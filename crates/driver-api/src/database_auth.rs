use crate::{ConnectionOptions, DriverError, ErrorKind, Result, Secret};
use serde::{Deserialize, Serialize};
use std::{io::Read, path::PathBuf};
use zeroize::Zeroizing;

fn default_command_timeout() -> u32 {
    10
}
#[derive(Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "method", rename_all = "snake_case", deny_unknown_fields)]
pub enum DatabaseAuthentication {
    #[default]
    Password,
    PgPass {
        #[serde(default)]
        path: Option<String>,
        #[serde(default)]
        hostname: Option<String>,
    },
    Command {
        command: String,
        #[serde(default)]
        working_directory: Option<String>,
        #[serde(default = "default_command_timeout")]
        timeout_seconds: u32,
    },
}
impl std::fmt::Debug for DatabaseAuthentication {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Password => f.write_str("Password"),
            Self::PgPass { path, .. } => f.debug_struct("PgPass").field("path", path).finish(),
            Self::Command {
                timeout_seconds, ..
            } => f
                .debug_struct("Command")
                .field("command", &"[REDACTED]")
                .field("timeout_seconds", timeout_seconds)
                .finish(),
        }
    }
}
impl DatabaseAuthentication {
    pub fn is_password(&self) -> bool {
        matches!(self, Self::Password)
    }
    pub fn validate(&self) -> Result<()> {
        let path_valid =
            |s: &str| !s.trim().is_empty() && s.len() <= 16 * 1024 && !s.contains('\0');
        if match self {
            Self::Password => false,
            Self::PgPass { path, hostname } => {
                path.as_deref().is_some_and(|p| !path_valid(p))
                    || hostname
                        .as_deref()
                        .is_some_and(|host| crate::tcp_host(host).is_err())
            }
            Self::Command {
                command,
                working_directory,
                timeout_seconds,
            } => {
                !path_valid(command)
                    || working_directory.as_deref().is_some_and(|p| !path_valid(p))
                    || !(1..=300).contains(timeout_seconds)
            }
        } {
            return Err(DriverError::new(
                ErrorKind::InvalidInput,
                "Invalid database authentication settings",
            ));
        }
        Ok(())
    }
    pub fn command_timeout(&self) -> std::time::Duration {
        std::time::Duration::from_secs(match self {
            Self::Command {
                timeout_seconds, ..
            } => u64::from(*timeout_seconds),
            _ => 0,
        })
    }
}
pub async fn resolve_database_authentication(
    authentication: &DatabaseAuthentication,
    options: &mut ConnectionOptions,
) -> Result<()> {
    authentication.validate()?;
    if authentication.is_password() {
        return Ok(());
    }
    match authentication {
        DatabaseAuthentication::Password => Ok(()),
        DatabaseAuthentication::PgPass { path, hostname } => {
            let ConnectionOptions::Postgres {
                ssh,
                host,
                port,
                database,
                user,
                password,
                ..
            } = options
            else {
                return Err(DriverError::new(
                    ErrorKind::InvalidInput,
                    "Passfile authentication requires PostgreSQL",
                ));
            };
            if password.is_some() && !user.is_empty() {
                return Ok(());
            }
            let path = passfile_path(path.as_deref())?;
            let host = if host.starts_with('/') {
                "localhost".to_owned()
            } else {
                crate::tcp_host(host)?.to_owned()
            };
            let mut hosts = Vec::new();
            if let Some(hostname) = hostname {
                hosts.push(crate::tcp_host(hostname)?.to_owned());
            } else if matches!(host.as_str(), "localhost" | "127.0.0.1" | "::1")
                && let Some(ssh) = ssh
            {
                hosts.push(crate::tcp_host(&ssh.host)?.to_owned());
            }
            if !hosts.contains(&host) {
                hosts.push(host);
            }
            let port = *port;
            let database = database.clone();
            let username = user.clone();
            let (matched_user, matched_password) = tokio::task::spawn_blocking(move || {
                read_passfile(path, &hosts, port, &database, &username)
            })
            .await
            .map_err(|_| auth_error("Cannot read PostgreSQL passfile"))??;
            *user = matched_user;
            if password.is_none() {
                *password = Some(matched_password);
            }
            Ok(())
        }
        DatabaseAuthentication::Command {
            command,
            working_directory,
            timeout_seconds,
        } => {
            let password = match options {
                ConnectionOptions::Postgres { user, password, .. } if !user.is_empty() => password,
                ConnectionOptions::Mysql { password, .. } => password,
                _ => {
                    return Err(DriverError::new(
                        ErrorKind::InvalidInput,
                        "Password command requires a network profile and PostgreSQL username",
                    ));
                }
            };
            if password.is_some() {
                return Ok(());
            }
            *password = Some(
                command_password(command, working_directory.as_deref(), *timeout_seconds).await?,
            );
            Ok(())
        }
    }
}
fn auth_error(message: &str) -> DriverError {
    DriverError::new(ErrorKind::Authentication, message)
}
fn passfile_path(path: Option<&str>) -> Result<PathBuf> {
    if let Some(path) = path {
        return Ok(path.into());
    }
    if let Some(path) = std::env::var_os("PGPASSFILE").filter(|p| !p.is_empty()) {
        return Ok(path.into());
    }
    #[cfg(windows)]
    let default =
        std::env::var_os("APPDATA").map(|p| PathBuf::from(p).join("postgresql/pgpass.conf"));
    #[cfg(not(windows))]
    let default = std::env::var_os("HOME").map(|p| PathBuf::from(p).join(".pgpass"));
    default.ok_or_else(|| auth_error("Cannot locate PostgreSQL passfile"))
}
fn os_username() -> Result<String> {
    let value =
        whoami::username().map_err(|_| auth_error("Cannot determine PostgreSQL username"))?;
    if value.is_empty() || value.len() > 16 * 1024 || value.contains('\0') {
        return Err(auth_error("Cannot determine PostgreSQL username"));
    }
    Ok(value)
}
fn passfile_fields(line: &str) -> Option<Vec<Zeroizing<String>>> {
    let mut fields = vec![Zeroizing::new(String::new())];
    let mut escaped = false;
    for ch in line.chars() {
        if escaped {
            fields.last_mut()?.push(ch);
            escaped = false;
        } else if ch == '\\' {
            escaped = true;
        } else if ch == ':' {
            fields.push(Zeroizing::new(String::new()));
        } else {
            fields.last_mut()?.push(ch);
        }
    }
    (!escaped && fields.len() == 5).then_some(fields)
}
fn read_passfile(
    path: PathBuf,
    hosts: &[String],
    port: u16,
    database: &str,
    user: &str,
) -> Result<(String, Secret)> {
    const LIMIT: u64 = 1024 * 1024;
    let metadata =
        std::fs::metadata(&path).map_err(|_| auth_error("Cannot read PostgreSQL passfile"))?;
    if !metadata.is_file() || metadata.len() > LIMIT {
        return Err(auth_error(
            "PostgreSQL passfile must be a regular file no larger than 1 MiB",
        ));
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        if metadata.permissions().mode() & 0o077 != 0 {
            return Err(auth_error(
                "PostgreSQL passfile permissions must exclude group and other users",
            ));
        }
    }
    let mut bytes = Zeroizing::new(Vec::new());
    std::fs::File::open(path)
        .and_then(|file| file.take(LIMIT + 1).read_to_end(&mut bytes))
        .map_err(|_| auth_error("Cannot read PostgreSQL passfile"))?;
    if bytes.len() > LIMIT as usize {
        return Err(auth_error("PostgreSQL passfile exceeds 1 MiB"));
    }
    let contents =
        std::str::from_utf8(&bytes).map_err(|_| auth_error("PostgreSQL passfile is not UTF-8"))?;
    let port = port.to_string();
    for host in hosts {
        for line in contents
            .lines()
            .filter(|line| !line.trim().is_empty() && !line.trim_start().starts_with('#'))
        {
            let Some(fields) = passfile_fields(line) else {
                continue;
            };
            let matches = |actual: &str, pattern: &str| pattern == "*" || actual == pattern;
            if !matches(host, &fields[0]) || !matches(&port, &fields[1]) {
                continue;
            }
            let candidate = if user.is_empty() {
                if *fields[3] == "*" {
                    os_username()?
                } else {
                    fields[3].to_string()
                }
            } else {
                user.to_owned()
            };
            let database = if database.is_empty() {
                candidate.as_str()
            } else {
                database
            };
            if !matches(database, &fields[2]) || !matches(&candidate, &fields[3]) {
                continue;
            }
            if candidate.is_empty()
                || candidate.len() > 16 * 1024
                || candidate.contains('\0')
                || fields[4].len() > 16 * 1024
                || fields[4].contains('\0')
            {
                return Err(auth_error("Invalid PostgreSQL passfile credentials"));
            }
            return Ok((candidate, Secret::new(fields[4].as_str())));
        }
    }
    Err(auth_error("No matching PostgreSQL passfile entry"))
}

async fn command_password(
    command: &str,
    directory: Option<&str>,
    timeout_seconds: u32,
) -> Result<Secret> {
    use std::process::Stdio;
    use tokio::io::AsyncReadExt;
    tokio::time::timeout(
        std::time::Duration::from_secs(u64::from(timeout_seconds)),
        async {
            #[cfg(windows)]
            let mut process = {
                let mut p = tokio::process::Command::new("cmd.exe");
                p.args(["/D", "/S", "/C", command]);
                p
            };
            #[cfg(not(windows))]
            let mut process = {
                let mut p = tokio::process::Command::new("/bin/sh");
                p.args(["-c", command]);
                p
            };
            #[cfg(unix)]
            process.process_group(0);
            if let Some(directory) = directory {
                process.current_dir(directory);
            }
            process
                .stdin(Stdio::null())
                .stdout(Stdio::piped())
                .stderr(Stdio::null())
                .kill_on_drop(true);
            let mut child = process
                .spawn()
                .map_err(|_| auth_error("Cannot start password command"))?;
            #[cfg(unix)]
            let mut group = CommandGroup(Some(nix::unistd::Pid::from_raw(
                i32::try_from(child.id().expect("running password command"))
                    .expect("OS process identifier"),
            )));
            let stdout = child.stdout.take().expect("piped command output");
            let mut output = Zeroizing::new(Vec::new());
            stdout
                .take(16385)
                .read_to_end(&mut output)
                .await
                .map_err(|_| auth_error("Cannot read password command output"))?;
            if output.len() > 16384 {
                return Err(auth_error("Password command output exceeds 16 KiB"));
            }
            let status = child.wait().await;
            // Once wait reaps the group leader its PID may be reused. Disarm
            // before yielding again; cancellation before reaping kills our group.
            #[cfg(unix)]
            {
                group.0 = None;
            }
            let status = status.map_err(|_| auth_error("Cannot wait for password command"))?;
            if !status.success() {
                return Err(auth_error("Password command failed"));
            }
            let output = std::str::from_utf8(&output)
                .map_err(|_| auth_error("Password command output is not UTF-8"))?;
            let line = Zeroizing::new(output.split('\n').next().unwrap_or("").replace('\r', ""));
            let password = line.trim();
            if password.is_empty() || password.contains('\0') {
                return Err(auth_error(
                    "Password command returned an empty or invalid password",
                ));
            }
            Ok(Secret::new(password))
        },
    )
    .await
    .map_err(|_| DriverError::new(ErrorKind::Timeout, "Password command timed out"))?
}

// Declared after Child so cancellation drops this guard before the child gets
// reaped. The process-group id is therefore still reserved by our child.
#[cfg(unix)]
struct CommandGroup(Option<nix::unistd::Pid>);
#[cfg(unix)]
impl Drop for CommandGroup {
    fn drop(&mut self) {
        if let Some(group) = self.0.take() {
            let _ = nix::sys::signal::killpg(group, nix::sys::signal::Signal::SIGKILL);
        }
    }
}
