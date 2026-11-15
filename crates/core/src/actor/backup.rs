use super::*;
use crate::{
    BACKUP_OUTPUT_TRUNCATED, BackupDirection, BackupPlan, BackupRequest, BackupTarget, ExportId,
    MAX_BACKUP_LINE_BYTES, MAX_BACKUP_OUTPUT_LINES, ToolCommand, backup_output_line, plan_backup,
};
use choscordb_export::Cancellation;
use std::process::Stdio;
use tokio::io::{AsyncRead, AsyncReadExt};

struct Output<'a> {
    events: &'a EventSink,
    run: ExportId,
    lines: usize,
}

impl Output<'_> {
    async fn line(&mut self, line: String) {
        let line = match self.lines {
            count if count < MAX_BACKUP_OUTPUT_LINES => line,
            MAX_BACKUP_OUTPUT_LINES => BACKUP_OUTPUT_TRUNCATED.to_owned(),
            _ => return,
        };
        self.lines += 1;
        send(
            self.events,
            Event::BackupOutput {
                run: self.run,
                line,
            },
        )
        .await
    }
}

#[derive(Default)]
struct Lines {
    partial: Vec<u8>,
}

impl Lines {
    fn push(&mut self, chunk: &[u8]) -> Vec<String> {
        let mut lines = Vec::new();
        for part in chunk.split_inclusive(|byte| *byte == b'\n') {
            let room = (MAX_BACKUP_LINE_BYTES + 2).saturating_sub(self.partial.len());
            self.partial
                .extend_from_slice(&part[..part.len().min(room)]);
            if part.ends_with(b"\n") {
                lines.push(backup_output_line(&self.partial));
                self.partial.clear();
            }
        }
        lines
    }
    fn finish(&mut self) -> Option<String> {
        (!self.partial.is_empty()).then(|| backup_output_line(&std::mem::take(&mut self.partial)))
    }
}

struct Stream<R> {
    reader: Option<R>,
    lines: Lines,
    buffer: Box<[u8; 8192]>,
}

impl<R: AsyncRead + Unpin> Stream<R> {
    fn new(reader: Option<R>) -> Self {
        Self {
            reader,
            lines: Lines::default(),
            buffer: Box::new([0; 8192]),
        }
    }
    async fn read(&mut self) -> std::io::Result<Vec<String>> {
        let Some(reader) = self.reader.as_mut() else {
            return Ok(Vec::new());
        };
        let read = reader.read(&mut self.buffer[..]).await?;
        if read == 0 {
            self.reader = None;
            return Ok(self.lines.finish().into_iter().collect());
        }
        Ok(self.lines.push(&self.buffer[..read]))
    }
}

fn stopped_message(direction: BackupDirection) -> &'static str {
    match direction {
        BackupDirection::Backup => "The backup was cancelled. The backup file can be incomplete.",
        BackupDirection::Restore => {
            "The restore was cancelled. The database can contain part of the backup."
        }
    }
}

async fn run_tool(
    command: ToolCommand,
    direction: BackupDirection,
    output: &mut Output<'_>,
    cancellation: &Cancellation,
    shutdown: &watch::Receiver<bool>,
) -> Result<()> {
    let name = command.name();
    output.line(command.preview()).await;
    let stdin = match &command.stdin {
        Some(path) => Stdio::from(std::fs::File::open(path).map_err(|error| {
            DriverError::new(
                ErrorKind::Io,
                format!("{} cannot be read: {error}", path.display()),
            )
        })?),
        None => Stdio::null(),
    };
    let mut process = tokio::process::Command::new(&command.program);
    process
        .args(&command.args)
        .envs(
            command
                .env
                .iter()
                .map(|(key, value)| (*key, value.as_str())),
        )
        .stdin(stdin)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .kill_on_drop(true);
    if let Some((key, secret)) = &command.password {
        process.env(key, secret.expose());
    }
    let mut child = process.spawn().map_err(|error| {
        DriverError::new(ErrorKind::Io, format!("{name} cannot start: {error}"))
    })?;
    let mut stdout = Stream::new(child.stdout.take());
    let mut stderr = Stream::new(child.stderr.take());
    let mut shutdown = shutdown.clone();
    let closed = async move { shutdown.wait_for(|stopped| *stopped).await.map(drop) };
    tokio::pin!(closed);
    let read_error =
        |error: std::io::Error| DriverError::new(ErrorKind::Io, format!("{name} output: {error}"));
    let status = loop {
        let lines = tokio::select! {
            biased;
            _ = cancellation.cancelled() => {
                child.kill().await.map_err(|error| DriverError::new(ErrorKind::Io, format!("{name} cannot stop: {error}")))?;
                return Err(DriverError::new(ErrorKind::Cancelled, stopped_message(direction)));
            }
            _ = &mut closed => {
                child.kill().await.map_err(|error| DriverError::new(ErrorKind::Io, format!("{name} cannot stop: {error}")))?;
                return Err(DriverError::new(ErrorKind::Disconnected, stopped_message(direction)));
            }
            lines = stdout.read(), if stdout.reader.is_some() => lines.map_err(read_error)?,
            lines = stderr.read(), if stderr.reader.is_some() => lines.map_err(read_error)?,
            status = child.wait(), if stdout.reader.is_none() && stderr.reader.is_none() => {
                break status.map_err(|error| DriverError::new(ErrorKind::Io, format!("{name} did not report its result: {error}")))?;
            }
        };
        for line in lines {
            output.line(line).await;
        }
    };
    match status.code() {
        Some(0) => Ok(()),
        Some(code) => Err(DriverError::new(
            ErrorKind::Query,
            format!("{name} stopped with exit code {code}. The output shows the cause."),
        )),
        None => Err(DriverError::new(
            ErrorKind::Query,
            format!("{name} stopped before it completed."),
        )),
    }
}

async fn run_plan(
    connection: &mut dyn Connection,
    target: &BackupTarget,
    request: &BackupRequest,
    output: &mut Output<'_>,
    cancellation: &Cancellation,
    shutdown: &watch::Receiver<bool>,
) -> Result<()> {
    let path = std::env::var_os("PATH");
    let plan = plan_backup(target, request, path.as_deref())
        .map_err(|message| DriverError::new(ErrorKind::InvalidInput, message))?;
    match plan {
        BackupPlan::SqliteVacuum { sql } => {
            output.line(sql.clone()).await;
            super::sql_file::execute(connection, &sql, cancellation)
                .await
                .map_err(|error| match error.kind {
                    ErrorKind::Cancelled => {
                        DriverError::new(ErrorKind::Cancelled, stopped_message(request.direction))
                    }
                    _ => error,
                })
        }
        BackupPlan::Tool(command) => {
            run_tool(command, request.direction, output, cancellation, shutdown).await
        }
    }
}

pub(super) async fn run(
    connection: &mut dyn Connection,
    target: &BackupTarget,
    run: ExportId,
    request: BackupRequest,
    cancellation: Cancellation,
    events: &EventSink,
    shutdown: &watch::Receiver<bool>,
) {
    let mut output = Output {
        events,
        run,
        lines: 0,
    };
    let outcome = run_plan(
        connection,
        target,
        &request,
        &mut output,
        &cancellation,
        shutdown,
    )
    .await;
    send(
        events,
        match outcome {
            Ok(()) => Event::BackupFinished { run },
            Err(error) => Event::BackupFailed { run, error },
        },
    )
    .await
}

#[cfg(test)]
mod tests {
    use super::*;

    async fn run_script(
        script: &str,
        password: Option<&str>,
        cancellation: &Cancellation,
    ) -> (Result<()>, Vec<String>) {
        let (sender, mut receiver) = mpsc::channel(MAX_BACKUP_OUTPUT_LINES * 2);
        let (_closed, shutdown) = watch::channel(false);
        let events = EventSink::new(sender, shutdown.clone());
        let run = ExportId {
            slot: 1,
            generation: 1,
        };
        let mut output = Output {
            events: &events,
            run,
            lines: 0,
        };
        let command = ToolCommand {
            program: "/bin/sh".into(),
            args: vec!["-c".into(), script.into()],
            env: vec![("BACKUP_MODE", "plain".into())],
            password: password.map(|value| ("BACKUP_SECRET", Secret::new(value))),
            stdin: None,
        };
        let outcome = run_tool(
            command,
            BackupDirection::Backup,
            &mut output,
            cancellation,
            &shutdown,
        )
        .await;
        drop(events);
        let mut lines = Vec::new();
        while let Some(Event::BackupOutput { line, .. }) = receiver.recv().await {
            lines.push(line);
        }
        (outcome, lines)
    }

    #[cfg(unix)]
    #[tokio::test]
    async fn a_tool_streams_its_output_and_fails_loudly() {
        let script = r#"echo "mode $BACKUP_MODE secret $BACKUP_SECRET"; echo warning >&2; exit 3"#;
        let (outcome, lines) = run_script(script, Some("s3cret"), &Cancellation::default()).await;
        assert_eq!(
            outcome.unwrap_err().message,
            "sh stopped with exit code 3. The output shows the cause."
        );
        assert!(
            lines[0].starts_with("$ BACKUP_SECRET=<hidden> /bin/sh -c "),
            "{lines:?}"
        );
        assert!(!lines[0].contains("s3cret"), "{lines:?}");
        let mut rest = lines[1..].to_vec();
        rest.sort();
        assert_eq!(rest, ["mode plain secret s3cret", "warning"]);
    }

    #[cfg(unix)]
    #[tokio::test]
    async fn a_cancelled_tool_stops() {
        let cancellation = Cancellation::default();
        cancellation.cancel();
        let (outcome, _) = run_script("sleep 30", None, &cancellation).await;
        let error = outcome.unwrap_err();
        assert_eq!(error.kind, ErrorKind::Cancelled);
        assert_eq!(
            error.message,
            "The backup was cancelled. The backup file can be incomplete."
        );
    }

    #[cfg(unix)]
    #[tokio::test]
    async fn output_stops_after_the_line_limit() {
        let script = format!("seq 1 {}", MAX_BACKUP_OUTPUT_LINES + 50);
        let (outcome, lines) = run_script(&script, None, &Cancellation::default()).await;
        outcome.unwrap();
        assert_eq!(lines.len(), MAX_BACKUP_OUTPUT_LINES + 1);
        assert_eq!(
            lines.last().map(String::as_str),
            Some(BACKUP_OUTPUT_TRUNCATED)
        );
    }

    #[test]
    fn lines_join_chunks_and_cap_long_lines() {
        let mut lines = Lines::default();
        assert!(lines.push(b"pg_dump: reading").is_empty());
        assert_eq!(
            lines.push(b" schemas\r\nnext\nlast"),
            ["pg_dump: reading schemas", "next"]
        );
        assert_eq!(lines.finish().as_deref(), Some("last"));
        assert_eq!(lines.finish(), None);
        let long = vec![b'x'; MAX_BACKUP_LINE_BYTES * 3];
        assert!(lines.push(&long).is_empty());
        let shown = lines.push(b"\n");
        assert_eq!(shown[0].len(), MAX_BACKUP_LINE_BYTES);
    }
}
