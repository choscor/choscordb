use choscordb_driver_api::{ConnectionOptions, Secret, TlsMode};
use std::ffi::{OsStr, OsString};
use std::path::{Path, PathBuf};

pub const MAX_BACKUP_OUTPUT_LINES: usize = 2_000;
pub const MAX_BACKUP_LINE_BYTES: usize = 4_096;
pub const BACKUP_OUTPUT_TRUNCATED: &str =
    "The output is too long. The tool continues, but ChoscorDB does not show more lines.";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BackupDirection {
    Backup,
    Restore,
}

impl BackupDirection {
    fn noun(self) -> &'static str {
        match self {
            Self::Backup => "backup",
            Self::Restore => "restore",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BackupOption {
    pub code: &'static str,
    pub label: &'static str,
    pub default: bool,
    on: &'static str,
    off: &'static str,
}

const fn option(
    code: &'static str,
    label: &'static str,
    default: bool,
    on: &'static str,
    off: &'static str,
) -> BackupOption {
    BackupOption {
        code,
        label,
        default,
        on,
        off,
    }
}

const POSTGRES_BACKUP: &[BackupOption] = &[
    option(
        "custom_format",
        "Custom archive format (needed by restore)",
        true,
        "--format=custom",
        "--format=plain",
    ),
    option("schema_only", "Schema only", false, "--schema-only", ""),
    option("data_only", "Data only", false, "--data-only", ""),
    option(
        "clean",
        "Drop objects before they are created",
        false,
        "--clean",
        "",
    ),
    option(
        "create",
        "Include the CREATE DATABASE statement",
        false,
        "--create",
        "",
    ),
    option(
        "no_owner",
        "Do not set object owners",
        false,
        "--no-owner",
        "",
    ),
];

const POSTGRES_RESTORE: &[BackupOption] = &[
    option(
        "single_transaction",
        "Restore in one transaction",
        true,
        "--single-transaction",
        "",
    ),
    option(
        "exit_on_error",
        "Stop at the first error",
        true,
        "--exit-on-error",
        "",
    ),
    option("schema_only", "Schema only", false, "--schema-only", ""),
    option("data_only", "Data only", false, "--data-only", ""),
    option(
        "clean",
        "Drop objects before they are created",
        false,
        "--clean",
        "",
    ),
    option(
        "no_owner",
        "Do not set object owners",
        false,
        "--no-owner",
        "",
    ),
];

const MYSQL_BACKUP: &[BackupOption] = &[
    option(
        "single_transaction",
        "Consistent snapshot (InnoDB)",
        true,
        "--single-transaction",
        "",
    ),
    option(
        "routines",
        "Include routines",
        true,
        "--routines",
        "--skip-routines",
    ),
    option(
        "triggers",
        "Include triggers",
        true,
        "--triggers",
        "--skip-triggers",
    ),
    option("events", "Include events", false, "--events", ""),
    option("schema_only", "Schema only", false, "--no-data", ""),
    option("data_only", "Data only", false, "--no-create-info", ""),
    option(
        "add_drop_table",
        "Drop each table before it is created",
        true,
        "--add-drop-table",
        "--skip-add-drop-table",
    ),
];

const MYSQL_RESTORE: &[BackupOption] = &[option(
    "force",
    "Continue after an error",
    false,
    "--force",
    "",
)];

pub fn backup_options(driver: &str, direction: BackupDirection) -> &'static [BackupOption] {
    match (driver, direction) {
        ("postgres", BackupDirection::Backup) => POSTGRES_BACKUP,
        ("postgres", BackupDirection::Restore) => POSTGRES_RESTORE,
        ("mysql" | "mariadb", BackupDirection::Backup) => MYSQL_BACKUP,
        ("mysql" | "mariadb", BackupDirection::Restore) => MYSQL_RESTORE,
        _ => &[],
    }
}

const SQLITE_RESTORE_UNAVAILABLE: &str = "ChoscorDB cannot restore over an open SQLite database. Open the backup file as a new connection.";

pub fn backup_uses_tool(driver: &str) -> bool {
    matches!(driver, "postgres" | "mysql" | "mariadb")
}

pub fn backup_unavailable(driver: &str, direction: BackupDirection) -> Option<&'static str> {
    match (driver, direction) {
        ("postgres" | "mysql" | "mariadb", _) | ("sqlite", BackupDirection::Backup) => None,
        ("sqlite", BackupDirection::Restore) => Some(SQLITE_RESTORE_UNAVAILABLE),
        _ => Some("This driver does not support backup and restore."),
    }
}

#[derive(Debug)]
pub struct BackupRequest {
    pub direction: BackupDirection,
    pub file: PathBuf,
    pub tool_folder: Option<PathBuf>,
    options: Vec<BackupOption>,
}

impl BackupRequest {
    pub fn new(
        driver: &str,
        direction: BackupDirection,
        file: &str,
        tool_folder: &str,
        selected: &[String],
    ) -> Result<Self, String> {
        if let Some(reason) = backup_unavailable(driver, direction) {
            return Err(reason.to_owned());
        }
        let file = PathBuf::from(file.trim());
        if file.as_os_str().is_empty() {
            return Err(format!("Choose a {} file.", direction.noun()));
        }
        if !file.is_absolute() {
            return Err("Enter the full path of the file.".to_owned());
        }
        let tool_folder = match tool_folder.trim() {
            "" => None,
            folder if Path::new(folder).is_absolute() => Some(PathBuf::from(folder)),
            _ => return Err("Enter the full path of the tool folder.".to_owned()),
        };
        let table = backup_options(driver, direction);
        let mut options = Vec::with_capacity(table.len());
        for code in selected {
            let Some(found) = table.iter().find(|candidate| candidate.code == code) else {
                return Err(format!("Unknown {} option: {code}", direction.noun()));
            };
            if !options.contains(found) {
                options.push(*found);
            }
        }
        let chosen = |code: &str| options.iter().any(|option| option.code == code);
        if chosen("schema_only") && chosen("data_only") {
            return Err("Choose schema only or data only, not both.".to_owned());
        }
        options.sort_by_key(|chosen| table.iter().position(|option| option == chosen));
        Ok(Self {
            direction,
            file,
            tool_folder,
            options,
        })
    }

    fn flags(&self, table: &[BackupOption]) -> Vec<String> {
        table
            .iter()
            .map(|option| {
                if self.options.contains(option) {
                    option.on
                } else {
                    option.off
                }
            })
            .filter(|flag| !flag.is_empty())
            .map(str::to_owned)
            .collect()
    }

    fn selected(&self, code: &str) -> bool {
        self.options.iter().any(|option| option.code == code)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum ServerFamily {
    Postgres,
    Mysql,
    Mariadb,
}

#[derive(Debug)]
pub struct ServerTarget {
    family: ServerFamily,
    host: String,
    port: u16,
    database: String,
    user: String,
    password: Option<Secret>,
    tls: TlsMode,
    root_certificate: Option<PathBuf>,
}

#[derive(Debug)]
pub enum BackupTarget {
    Sqlite,
    Server(Box<ServerTarget>),
    Unavailable(&'static str),
}

impl BackupTarget {
    pub fn from_options(options: &ConnectionOptions) -> Self {
        let (
            family,
            proxy,
            host,
            port,
            database,
            user,
            password,
            tls,
            root_certificate,
            tls_identity,
            ssh,
        ) = match options {
            ConnectionOptions::Sqlite { .. } => return Self::Sqlite,
            ConnectionOptions::Postgres {
                proxy,
                host,
                port,
                database,
                user,
                password,
                tls,
                root_certificate,
                tls_identity,
                ssh,
                ..
            } => (
                ServerFamily::Postgres,
                proxy,
                host,
                port,
                database,
                user,
                password,
                tls,
                root_certificate,
                tls_identity,
                ssh,
            ),
            ConnectionOptions::Mysql {
                proxy,
                host,
                port,
                database,
                user,
                password,
                tls,
                root_certificate,
                tls_identity,
                ssh,
                ..
            } => (
                ServerFamily::Mysql,
                proxy,
                host,
                port,
                database,
                user,
                password,
                tls,
                root_certificate,
                tls_identity,
                ssh,
            ),
            ConnectionOptions::Mariadb {
                proxy,
                host,
                port,
                database,
                user,
                password,
                tls,
                root_certificate,
                tls_identity,
                ssh,
                ..
            } => (
                ServerFamily::Mariadb,
                proxy,
                host,
                port,
                database,
                user,
                password,
                tls,
                root_certificate,
                tls_identity,
                ssh,
            ),
        };
        if ssh.is_some() {
            return Self::Unavailable(
                "Backup and restore cannot use an SSH tunnel. The client tools connect to the server directly.",
            );
        }
        if proxy.is_some() {
            return Self::Unavailable(
                "Backup and restore cannot use a SOCKS proxy. The client tools connect to the server directly.",
            );
        }
        if tls_identity.is_some() {
            return Self::Unavailable("Backup and restore cannot use a client certificate.");
        }
        Self::Server(Box::new(ServerTarget {
            family,
            host: host.clone(),
            port: *port,
            database: database.clone(),
            user: user.clone(),
            password: password.as_ref().map(|secret| Secret::new(secret.expose())),
            tls: tls.clone(),
            root_certificate: root_certificate.clone(),
        }))
    }
}

#[derive(Debug)]
pub struct ToolCommand {
    pub program: PathBuf,
    pub args: Vec<String>,
    pub env: Vec<(&'static str, String)>,
    pub password: Option<(&'static str, Secret)>,
    pub stdin: Option<PathBuf>,
}

impl ToolCommand {
    pub fn preview(&self) -> String {
        let mut parts = Vec::with_capacity(self.args.len() + 3);
        if let Some((name, _)) = &self.password {
            parts.push(format!("{name}=<hidden>"));
        }
        parts.push(shell_word(&self.program.to_string_lossy()));
        parts.extend(self.args.iter().map(|arg| shell_word(arg)));
        if let Some(stdin) = &self.stdin {
            parts.push(format!("< {}", shell_word(&stdin.to_string_lossy())));
        }
        format!("$ {}", parts.join(" "))
    }
    pub fn name(&self) -> String {
        self.program
            .file_stem()
            .map_or_else(String::new, |name| name.to_string_lossy().into_owned())
    }
}

fn shell_word(word: &str) -> String {
    if !word.is_empty()
        && word
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || "-_=./:,@+".contains(c))
    {
        word.to_owned()
    } else {
        format!("'{}'", word.replace('\'', r"'\''"))
    }
}

#[derive(Debug)]
pub enum BackupPlan {
    SqliteVacuum { sql: String },
    Tool(ToolCommand),
}

pub fn plan_backup(
    target: &BackupTarget,
    request: &BackupRequest,
    path: Option<&OsStr>,
) -> Result<BackupPlan, String> {
    match target {
        BackupTarget::Unavailable(reason) => Err((*reason).to_owned()),
        BackupTarget::Sqlite => plan_sqlite(request),
        BackupTarget::Server(server) => {
            check_file(request)?;
            let program = find_tool(server.family, request, path)?;
            Ok(BackupPlan::Tool(match server.family {
                ServerFamily::Postgres => postgres_command(server, request, program)?,
                ServerFamily::Mysql | ServerFamily::Mariadb => {
                    mysql_command(server, request, program)?
                }
            }))
        }
    }
}

fn plan_sqlite(request: &BackupRequest) -> Result<BackupPlan, String> {
    if request.direction == BackupDirection::Restore {
        return Err(SQLITE_RESTORE_UNAVAILABLE.to_owned());
    }
    if request.file.exists() {
        return Err("Choose a new file. SQLite cannot back up into an existing file.".to_owned());
    }
    check_file(request)?;
    let path = request
        .file
        .to_str()
        .ok_or("The backup path must be UTF-8 text.")?;
    Ok(BackupPlan::SqliteVacuum {
        sql: format!("VACUUM INTO '{}'", path.replace('\'', "''")),
    })
}

fn check_file(request: &BackupRequest) -> Result<(), String> {
    match request.direction {
        BackupDirection::Backup => match request.file.parent() {
            Some(folder) if folder.is_dir() => Ok(()),
            _ => Err(format!(
                "The folder of {} does not exist.",
                request.file.display()
            )),
        },
        BackupDirection::Restore if request.file.is_file() => Ok(()),
        BackupDirection::Restore => Err(format!("{} is not a file.", request.file.display())),
    }
}

fn tool_names(family: ServerFamily, direction: BackupDirection) -> &'static [&'static str] {
    match (family, direction) {
        (ServerFamily::Postgres, BackupDirection::Backup) => &["pg_dump"],
        (ServerFamily::Postgres, BackupDirection::Restore) => &["pg_restore"],
        (ServerFamily::Mysql, BackupDirection::Backup) => &["mysqldump"],
        (ServerFamily::Mysql, BackupDirection::Restore) => &["mysql"],
        (ServerFamily::Mariadb, BackupDirection::Backup) => &["mariadb-dump", "mysqldump"],
        (ServerFamily::Mariadb, BackupDirection::Restore) => &["mariadb", "mysql"],
    }
}

fn find_tool(
    family: ServerFamily,
    request: &BackupRequest,
    path: Option<&OsStr>,
) -> Result<PathBuf, String> {
    let names = tool_names(family, request.direction);
    let folders: Vec<PathBuf> = match &request.tool_folder {
        Some(folder) => vec![folder.clone()],
        None => path
            .map(|path| std::env::split_paths(path).collect())
            .unwrap_or_default(),
    };
    for name in names {
        let file = OsString::from(format!("{name}{}", std::env::consts::EXE_SUFFIX));
        if let Some(found) = folders
            .iter()
            .map(|folder| folder.join(&file))
            .find(|candidate| candidate.is_file())
        {
            return Ok(found);
        }
    }
    let name = names[0];
    let client = match family {
        ServerFamily::Postgres => "PostgreSQL",
        ServerFamily::Mysql => "MySQL",
        ServerFamily::Mariadb => "MariaDB",
    };
    Err(match &request.tool_folder {
        Some(folder) => format!(
            "{name} is not in {}. Choose the folder that contains the {client} client tools.",
            folder.display()
        ),
        None => format!(
            "{name} is not on PATH. Install the {client} client tools, or choose the folder that contains them."
        ),
    })
}

fn database(server: &ServerTarget) -> Result<String, String> {
    match server.database.as_str() {
        "" => Err("Enter a database in the connection.".to_owned()),
        name if name.starts_with('-') || name.contains('\0') => {
            Err("The database name cannot start with a hyphen.".to_owned())
        }
        name => Ok(name.to_owned()),
    }
}

fn postgres_command(
    server: &ServerTarget,
    request: &BackupRequest,
    program: PathBuf,
) -> Result<ToolCommand, String> {
    let database = database(server)?;
    let mut args = vec![
        format!("--host={}", server.host),
        format!("--port={}", server.port),
        format!("--username={}", server.user),
        format!("--dbname={database}"),
        "--no-password".to_owned(),
        "--verbose".to_owned(),
    ];
    match request.direction {
        BackupDirection::Backup => {
            args.extend(request.flags(POSTGRES_BACKUP));
            args.push(format!("--file={}", path_text(&request.file)?));
        }
        BackupDirection::Restore => {
            if !postgres_archive(&request.file)? {
                return Err("pg_restore needs a custom or tar archive. Run a plain SQL backup with Run SQL file.".to_owned());
            }
            args.extend(request.flags(POSTGRES_RESTORE));
            args.push(path_text(&request.file)?);
        }
    }
    let mut env = vec![(
        "PGSSLMODE",
        match server.tls {
            TlsMode::Disable => "disable",
            TlsMode::Prefer => "prefer",
            TlsMode::Require => "require",
            TlsMode::VerifyCa => "verify-ca",
            TlsMode::VerifyFull => "verify-full",
        }
        .to_owned(),
    )];
    if let Some(certificate) = &server.root_certificate {
        env.push(("PGSSLROOTCERT", path_text(certificate)?));
    }
    Ok(ToolCommand {
        program,
        args,
        env,
        password: password("PGPASSWORD", server),
        stdin: None,
    })
}

fn postgres_archive(file: &Path) -> Result<bool, String> {
    use std::io::Read;
    let mut header = Vec::with_capacity(262);
    std::fs::File::open(file)
        .and_then(|file| file.take(262).read_to_end(&mut header))
        .map_err(|error| format!("{} cannot be read: {error}", file.display()))?;
    Ok(header.starts_with(b"PGDMP") || header.get(257..262) == Some(b"ustar".as_slice()))
}

fn mysql_command(
    server: &ServerTarget,
    request: &BackupRequest,
    program: PathBuf,
) -> Result<ToolCommand, String> {
    let database = database(server)?;
    let mut args = vec![
        format!("--host={}", server.host),
        format!("--port={}", server.port),
        format!("--user={}", server.user),
    ];
    args.extend(mysql_tls(server)?);
    let stdin = match request.direction {
        BackupDirection::Backup => {
            args.push("--verbose".to_owned());
            args.extend(request.flags(MYSQL_BACKUP));
            args.push(format!("--result-file={}", path_text(&request.file)?));
            args.push(database);
            None
        }
        BackupDirection::Restore => {
            args.push(format!("--database={database}"));
            if request.selected("force") {
                args.push("--force".to_owned());
            }
            Some(request.file.clone())
        }
    };
    Ok(ToolCommand {
        program,
        args,
        env: Vec::new(),
        password: password("MYSQL_PWD", server),
        stdin,
    })
}

fn mysql_tls(server: &ServerTarget) -> Result<Vec<String>, String> {
    let mut args: Vec<String> = match (server.family, &server.tls) {
        (_, TlsMode::Prefer) => Vec::new(),
        (ServerFamily::Mariadb, TlsMode::Disable) => vec!["--skip-ssl".into()],
        (ServerFamily::Mariadb, TlsMode::Require) => vec!["--ssl".into()],
        (ServerFamily::Mariadb, TlsMode::VerifyCa | TlsMode::VerifyFull) => {
            vec!["--ssl".into(), "--ssl-verify-server-cert".into()]
        }
        (_, TlsMode::Disable) => vec!["--ssl-mode=DISABLED".into()],
        (_, TlsMode::Require) => vec!["--ssl-mode=REQUIRED".into()],
        (_, TlsMode::VerifyCa) => vec!["--ssl-mode=VERIFY_CA".into()],
        (_, TlsMode::VerifyFull) => vec!["--ssl-mode=VERIFY_IDENTITY".into()],
    };
    if let Some(certificate) = &server.root_certificate {
        args.push(format!("--ssl-ca={}", path_text(certificate)?));
    }
    Ok(args)
}

fn password(name: &'static str, server: &ServerTarget) -> Option<(&'static str, Secret)> {
    server
        .password
        .as_ref()
        .map(|secret| (name, Secret::new(secret.expose())))
}

fn path_text(path: &Path) -> Result<String, String> {
    path.to_str()
        .map(str::to_owned)
        .ok_or_else(|| format!("{} must be UTF-8 text.", path.display()))
}

pub fn backup_output_line(line: &[u8]) -> String {
    let line = line.strip_suffix(b"\n").unwrap_or(line);
    let line = line.strip_suffix(b"\r").unwrap_or(line);
    let text = String::from_utf8_lossy(&line[..line.len().min(MAX_BACKUP_LINE_BYTES)]);
    text.into_owned()
}
