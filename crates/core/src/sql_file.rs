use choscordb_sql_language::{MysqlSqlMode, split_script};
use std::path::PathBuf;

pub const SQL_FILE_READ_BYTES: usize = 1 << 20;
pub const MAX_SQL_FILE_STATEMENT_BYTES: usize = 64 << 20;
pub const MAX_SQL_FILE_ERRORS: usize = 100;
pub const SQL_FILE_PROGRESS_STATEMENTS: u64 = 100;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SqlFileMode {
    StopOnError,
    ContinueOnError,
    Transaction,
}

impl SqlFileMode {
    pub const ALL: [Self; 3] = [Self::StopOnError, Self::ContinueOnError, Self::Transaction];
    pub fn code(self) -> &'static str {
        match self {
            Self::StopOnError => "stop",
            Self::ContinueOnError => "continue",
            Self::Transaction => "transaction",
        }
    }
    pub fn label(self) -> &'static str {
        match self {
            Self::StopOnError => "Stop at the first error",
            Self::ContinueOnError => "Continue after errors",
            Self::Transaction => "Run in one transaction and roll back on error",
        }
    }
    pub fn from_code(code: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|mode| mode.code() == code)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SqlFileRun {
    pub file: PathBuf,
    pub mysql: Option<MysqlSqlMode>,
    pub mode: SqlFileMode,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SqlFileStatement {
    pub line: u64,
    pub sql: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SqlFileError {
    pub line: u64,
    pub message: String,
}

pub struct SqlFileSplitter {
    mysql: Option<MysqlSqlMode>,
    pending: Vec<u8>,
    delimiter: String,
    line: u64,
    started: bool,
}

fn lines(text: &str) -> u64 {
    text.bytes().filter(|byte| *byte == b'\n').count() as u64
}

impl SqlFileSplitter {
    pub fn new(mysql: Option<MysqlSqlMode>) -> Self {
        Self {
            mysql,
            pending: Vec::new(),
            delimiter: ";".into(),
            line: 1,
            started: false,
        }
    }

    pub fn push(
        &mut self,
        bytes: &[u8],
        at_end: bool,
    ) -> Result<Vec<SqlFileStatement>, SqlFileError> {
        self.pending.extend_from_slice(bytes);
        if !self.started && (self.pending.len() >= 3 || at_end) {
            self.started = true;
            if self.pending.starts_with(b"\xef\xbb\xbf") {
                self.pending.drain(..3);
            }
        }
        let text = match std::str::from_utf8(&self.pending) {
            Ok(text) => text,
            Err(error) if error.error_len().is_none() && !at_end => {
                std::str::from_utf8(&self.pending[..error.valid_up_to()]).map_err(|_| {
                    SqlFileError {
                        line: self.line,
                        message: "The file is not UTF-8 text.".into(),
                    }
                })?
            }
            Err(error) => {
                let valid = String::from_utf8_lossy(&self.pending[..error.valid_up_to()]);
                return Err(SqlFileError {
                    line: self.line + lines(&valid),
                    message: "The file is not UTF-8 text.".into(),
                });
            }
        };
        let complete = at_end && text.len() == self.pending.len();
        let chunk = split_script(text, self.mysql, &self.delimiter, complete).map_err(|error| {
            SqlFileError {
                line: self.line + lines(&text[..error.offset]),
                message: error.message,
            }
        })?;
        let mut statements = Vec::with_capacity(chunk.statements.len());
        let (mut counted, mut line) = (0, self.line);
        for range in chunk.statements {
            line += lines(&text[counted..range.start]);
            counted = range.start;
            statements.push(SqlFileStatement {
                line,
                sql: text[range].to_owned(),
            });
        }
        self.line = line + lines(&text[counted..chunk.consumed]);
        self.delimiter = chunk.delimiter;
        self.pending.drain(..chunk.consumed);
        if self.pending.len() > MAX_SQL_FILE_STATEMENT_BYTES {
            return Err(SqlFileError {
                line: self.line,
                message: format!(
                    "A statement is larger than {} MiB.",
                    MAX_SQL_FILE_STATEMENT_BYTES >> 20
                ),
            });
        }
        Ok(statements)
    }
}
