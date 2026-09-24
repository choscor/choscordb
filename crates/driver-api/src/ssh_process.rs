use std::process::ExitStatus;
use tokio::process::{Child, Command};
/// An unreaped group leader pins the Unix process-group identifier until cleanup.
pub(crate) struct OwnedChild {
    child: Option<Child>,
    #[cfg(unix)]
    group: Option<nix::unistd::Pid>,
}
impl OwnedChild {
    pub(crate) fn spawn(command: &mut Command) -> std::io::Result<Self> {
        command.kill_on_drop(true);
        #[cfg(unix)]
        command.process_group(0);
        let child = command.spawn()?;
        #[cfg(unix)]
        let group = child
            .id()
            .and_then(|id| i32::try_from(id).ok())
            .map(nix::unistd::Pid::from_raw);
        Ok(Self {
            child: Some(child),
            #[cfg(unix)]
            group,
        })
    }
    pub(crate) fn child(&mut self) -> &mut Child {
        self.child.as_mut().expect("owned SSH child")
    }
    #[cfg(unix)]
    pub(crate) fn try_wait(&mut self) -> std::io::Result<Option<ExitStatus>> {
        let result = self.child().try_wait();
        // Reaping releases the leader's numeric ID, just as with wait().
        #[cfg(unix)]
        if matches!(result, Ok(Some(_))) {
            self.group = None;
        }
        result
    }
    pub(crate) async fn wait(&mut self) -> std::io::Result<ExitStatus> {
        let result = self.child().wait().await;
        // Never signal a group after reaping its leader: its numeric ID can be reused.
        #[cfg(unix)]
        if result.is_ok() {
            self.group = None;
        }
        result
    }
}
impl Drop for OwnedChild {
    fn drop(&mut self) {
        #[cfg(unix)]
        if let Some(group) = self.group.take() {
            let _ = nix::sys::signal::killpg(group, nix::sys::signal::Signal::SIGKILL);
        }
        if let Some(mut child) = self.child.take() {
            let _ = child.start_kill();
            if let Ok(runtime) = tokio::runtime::Handle::try_current() {
                runtime.spawn(async move {
                    let _ = child.wait().await;
                });
            }
        }
    }
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;

    #[tokio::test]
    async fn reaped_failed_master_does_not_retain_process_group_ownership() {
        let mut command = Command::new("sh");
        command.args(["-c", "exit 17"]);
        let mut child = OwnedChild::spawn(&mut command).unwrap();
        let status = tokio::time::timeout(std::time::Duration::from_secs(2), async {
            loop {
                if let Some(status) = child.try_wait().unwrap() {
                    break status;
                }
                tokio::task::yield_now().await;
            }
        })
        .await
        .unwrap();
        assert_eq!(status.code(), Some(17));
        assert!(
            child.group.is_none(),
            "reaped leader no longer reserves its process-group ID"
        );
    }
}
