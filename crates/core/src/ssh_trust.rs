use crate::{ConnectionProfile, Engine, Event, ProfileConfiguration, ProfileSecrets, SubmitError};
use choscordb_driver_api::{
    DriverError, ErrorKind, SshHostKeyCandidate, SshHostKeyTarget, SshIdentitySource,
};
use std::path::PathBuf;

impl Engine {
    pub fn inspect_profile_ssh_host_keys(
        &mut self,
        profile: ConnectionProfile,
        mut secrets: ProfileSecrets,
        target: SshHostKeyTarget,
        request_token: u64,
    ) -> Result<(), SubmitError> {
        self.ensure_running()?;
        profile.validate().map_err(|_| SubmitError::InvalidInput)?;
        let ssh = match &profile.configuration {
            ProfileConfiguration::Postgres { ssh: Some(ssh), .. }
            | ProfileConfiguration::Mysql { ssh: Some(ssh), .. } => ssh.clone(),
            _ => return Err(SubmitError::InvalidInput),
        };
        let count = match &target {
            SshHostKeyTarget::Target => ssh.options.jump_hosts.len(),
            SshHostKeyTarget::Jump(id) => ssh
                .options
                .jump_hosts
                .iter()
                .position(|hop| hop.id.as_deref() == Some(id))
                .ok_or(SubmitError::InvalidInput)?,
            SshHostKeyTarget::JumpIndex(index) if *index < ssh.options.jump_hosts.len() => *index,
            SshHostKeyTarget::JumpIndex(_) => return Err(SubmitError::InvalidInput),
        };
        if secrets.database.is_some()
            || secrets.ssh.is_some()
            || secrets.ssh_private_key.is_some()
            || secrets.tls.is_some()
            || secrets.proxy.is_some()
        {
            return Err(SubmitError::InvalidInput);
        }
        let prefix = ssh
            .options
            .jump_hosts
            .iter()
            .take(count)
            .filter_map(|hop| hop.id.as_deref())
            .collect::<std::collections::BTreeSet<_>>();
        if secrets
            .ssh_jumps
            .keys()
            .any(|id| !prefix.contains(id.as_str()))
            || secrets
                .ssh_jump_private_keys
                .keys()
                .any(|id| !prefix.contains(id.as_str()))
        {
            return Err(SubmitError::InvalidInput);
        }
        for hop in ssh.options.jump_hosts.iter().take(count) {
            let Some(id) = hop.id.as_ref() else { continue };
            if let Some(secret) = secrets.ssh_jumps.get(id)
                && (!hop.authentication.uses_secret()
                    || secret.expose().len() > 16 * 1024
                    || secret.expose().contains(['\0', '\r', '\n']))
            {
                return Err(SubmitError::InvalidInput);
            }
            if let Some(key) = secrets.ssh_jump_private_keys.get(id)
                && (hop.authentication != choscordb_driver_api::SshJumpAuthentication::PublicKey
                    || hop.identity_source != SshIdentitySource::Inline
                    || key.expose().trim().is_empty()
                    || key.expose().len() > choscordb_credentials::MAX_SECRET_BYTES
                    || key.expose().contains('\0'))
            {
                return Err(SubmitError::InvalidInput);
            }
        }
        let permit = self
            .profile_tests
            .clone()
            .try_acquire_owned()
            .map_err(|_| SubmitError::ResourceLimit)?;
        let commands = self.profiles.clone();
        let events = self.events_tx.clone();
        let mut shutdown = self.shutdown.subscribe();
        let timeout =
            std::time::Duration::from_secs(u64::from(ssh.options.connect_timeout_seconds));
        self.runtime
            .as_ref()
            .ok_or(SubmitError::ShuttingDown)?
            .spawn(async move {
                let _permit = permit;
                let result = tokio::select! { biased;
                    _ = shutdown.wait_for(|stopped| *stopped) => return,
                    result = tokio::time::timeout(timeout, async {
                        let mut hop_secrets = std::collections::BTreeMap::new();
                        let mut hop_keys = std::collections::BTreeMap::new();
                        for hop in ssh.options.jump_hosts.iter().take(count) {
                            let Some(id) = hop.id.as_ref() else { continue };
                            if hop.authentication.uses_secret()
                                && let Some(secret) = crate::profiles::resolve(
                                    &commands,
                                    profile.ssh_jump_credential_refs.get(id).cloned(),
                                    secrets.ssh_jumps.remove(id),
                                ).await?
                            {
                                hop_secrets.insert(id.clone(), secret);
                            }
                            if hop.authentication == choscordb_driver_api::SshJumpAuthentication::PublicKey
                                && hop.identity_source == SshIdentitySource::Inline
                                && let Some(key) = crate::profiles::resolve(
                                    &commands,
                                    profile.ssh_jump_private_key_refs.get(id).cloned(),
                                    secrets.ssh_jump_private_keys.remove(id),
                                ).await?
                            {
                                hop_keys.insert(id.clone(), key);
                            }
                        }
                        choscordb_driver_api::inspect_ssh_host_keys(&ssh, target, hop_secrets, hop_keys).await
                    }) => result.unwrap_or_else(|_| Err(DriverError::new(ErrorKind::Timeout, "SSH host key inspection timed out"))),
                };
                let event = match result {
                    Ok(candidates) => Event::SshHostKeysInspected { request_token, candidates },
                    Err(error) => Event::SshHostKeyFailed { request_token, error },
                };
                tokio::select! { biased;
                    _ = shutdown.wait_for(|stopped| *stopped) => {},
                    _ = events.send(event) => {},
                }
            });
        Ok(())
    }

    pub fn approve_ssh_host_key(
        &mut self,
        candidate: SshHostKeyCandidate,
        expected_sha256: String,
        known_hosts_path: PathBuf,
        request_token: u64,
    ) -> Result<(), SubmitError> {
        self.ensure_running()?;
        let events = self.events_tx.clone();
        let mut shutdown = self.shutdown.subscribe();
        self.runtime
            .as_ref()
            .ok_or(SubmitError::ShuttingDown)?
            .spawn(async move {
                let result = tokio::select! { biased;
                    _ = shutdown.wait_for(|stopped| *stopped) => return,
                    result = choscordb_driver_api::approve_ssh_host_key(
                        &candidate, &expected_sha256, &known_hosts_path) => result,
                };
                let event = match result {
                    Ok(outcome) => Event::SshHostKeyApproved {
                        request_token,
                        outcome,
                    },
                    Err(error) => Event::SshHostKeyFailed {
                        request_token,
                        error,
                    },
                };
                tokio::select! { biased;
                    _ = shutdown.wait_for(|stopped| *stopped) => {},
                    _ = events.send(event) => {},
                }
            });
        Ok(())
    }
}
