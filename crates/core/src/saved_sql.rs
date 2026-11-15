use crate::document_io::{
    DocumentIoError, is_sql_path, open_saved_root, read_saved_sql_document, write_sql_document,
};
use cap_fs_ext::{DirExt, FollowSymlinks, OpenOptionsFollowExt, OpenOptionsSyncExt};
use cap_std::fs::Dir;
use std::{
    cmp::Ordering,
    collections::HashMap,
    io::Read,
    path::{Component, Path, PathBuf},
    time::SystemTime,
};

pub const MAX_SAVED_SQL_DOCUMENTS: usize = 1000;
pub const MAX_SAVED_SQL_SCAN_ENTRIES: usize = 100_000;
pub const MAX_SNIPPET_FILE_BYTES: u64 = 64 * 1024;
pub const MAX_SNIPPET_TOTAL_BYTES: usize = 8 * 1024 * 1024;
pub const MAX_SNIPPET_KEYWORD_CHARS: usize = 64;
const KEYWORD_PREFIX: &str = "-- keyword:";
const KEYWORD_RULE: &str = "A keyword has 1 to 64 letters, digits or underscores.";
const FOLDER_NAME_RULE: &str = "Enter a folder name without slashes. It cannot start with a dot.";

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SavedSqlSort {
    Name,
    Modified,
}

impl SavedSqlSort {
    pub const ALL: [Self; 2] = [Self::Name, Self::Modified];
    pub fn code(self) -> &'static str {
        match self {
            Self::Name => "name",
            Self::Modified => "modified",
        }
    }
    pub fn label(self) -> &'static str {
        match self {
            Self::Name => "Sort by name",
            Self::Modified => "Sort by date modified",
        }
    }
    pub fn from_code(code: &str) -> Option<Self> {
        Self::ALL.into_iter().find(|sort| sort.code() == code)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SavedSqlNode {
    pub path: PathBuf,
    pub relative_path: PathBuf,
    pub folder: bool,
    pub keyword: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SavedSqlSnippet {
    pub keyword: String,
    pub sql: String,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct SavedSqlTree {
    pub nodes: Vec<SavedSqlNode>,
    pub snippets: Vec<SavedSqlSnippet>,
    pub errors: Vec<String>,
    pub has_more: bool,
}

struct Entry {
    relative_path: PathBuf,
    folder: bool,
    modified: Option<SystemTime>,
    head: Option<Result<String, String>>,
}

fn valid_keyword(keyword: &str) -> bool {
    (1..=MAX_SNIPPET_KEYWORD_CHARS).contains(&keyword.chars().count())
        && keyword.chars().all(|c| c.is_alphanumeric() || c == '_')
}

fn split_keyword(text: &str) -> Option<(&str, &str)> {
    let (first, rest) = text.split_once('\n').unwrap_or((text, ""));
    let first = first.trim_end_matches('\r');
    let keyword = first
        .get(..KEYWORD_PREFIX.len())
        .filter(|prefix| prefix.eq_ignore_ascii_case(KEYWORD_PREFIX))
        .map(|_| first[KEYWORD_PREFIX.len()..].trim())?;
    Some((keyword, rest))
}

fn read_head(file: cap_std::fs::File, size: u64) -> Option<Result<String, String>> {
    let limit = if size <= MAX_SNIPPET_FILE_BYTES {
        size
    } else {
        KEYWORD_PREFIX.len() as u64 + MAX_SNIPPET_KEYWORD_CHARS as u64 * 4 + 2
    };
    let mut bytes = Vec::new();
    if let Err(error) = file.take(limit).read_to_end(&mut bytes) {
        return Some(Err(error.to_string()));
    }
    let text = String::from_utf8_lossy(&bytes);
    split_keyword(&text)?;
    if size > MAX_SNIPPET_FILE_BYTES {
        return Some(Err(format!(
            "A snippet file is limited to {} KiB.",
            MAX_SNIPPET_FILE_BYTES / 1024
        )));
    }
    Some(String::from_utf8(bytes).map_err(|_| "The file is not UTF-8 text.".to_owned()))
}

fn scan(root: &Path) -> Result<(Vec<Entry>, bool), DocumentIoError> {
    let root_directory = match open_saved_root(root) {
        Ok(directory) => directory,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
            return Ok((Vec::new(), false));
        }
        Err(error) => return Err(error.into()),
    };
    let mut entries = Vec::new();
    let mut pending: Vec<(Dir, PathBuf)> = vec![(root_directory, PathBuf::new())];
    while let Some((directory, relative_dir)) = pending.pop() {
        let listing = match directory.entries() {
            Ok(listing) => listing,
            Err(_) if !relative_dir.as_os_str().is_empty() => continue,
            Err(error) => return Err(error.into()),
        };
        for entry in listing {
            if entries.len() >= MAX_SAVED_SQL_SCAN_ENTRIES {
                return Ok((entries, true));
            }
            let entry = entry?;
            let Ok(file_type) = entry.file_type() else {
                continue;
            };
            if file_type.is_symlink() {
                continue;
            }
            let name = entry.file_name();
            let relative_path = relative_dir.join(&name);
            if file_type.is_dir() {
                if let Ok(child) = directory.open_dir_nofollow(&name) {
                    entries.push(Entry {
                        relative_path: relative_path.clone(),
                        folder: true,
                        modified: None,
                        head: None,
                    });
                    pending.push((child, relative_path));
                }
            } else if file_type.is_file() && is_sql_path(&relative_path) {
                let mut options = cap_std::fs::OpenOptions::new();
                options.read(true).follow(FollowSymlinks::No).nonblock(true);
                let Ok(file) = directory.open_with(&name, &options) else {
                    continue;
                };
                let Ok(metadata) = file.metadata() else {
                    continue;
                };
                if !metadata.is_file() {
                    continue;
                }
                let modified = metadata.modified().ok().map(|time| time.into_std());
                entries.push(Entry {
                    relative_path,
                    folder: false,
                    modified,
                    head: read_head(file, metadata.len()),
                });
            }
        }
    }
    Ok((entries, false))
}

fn compare_names(left: &Path, right: &Path) -> Ordering {
    let name = |path: &Path| {
        path.file_name()
            .map(|name| name.to_string_lossy().to_lowercase())
            .unwrap_or_default()
    };
    name(left).cmp(&name(right)).then_with(|| left.cmp(right))
}

pub fn saved_sql_tree(root: &Path, sort: SavedSqlSort) -> Result<SavedSqlTree, DocumentIoError> {
    let (entries, truncated) = scan(root)?;
    let mut children: HashMap<PathBuf, Vec<usize>> = HashMap::new();
    for (index, entry) in entries.iter().enumerate() {
        let parent = entry
            .relative_path
            .parent()
            .map(Path::to_path_buf)
            .unwrap_or_default();
        children.entry(parent).or_default().push(index);
    }
    for list in children.values_mut() {
        list.sort_by(|&a, &b| {
            let (a, b) = (&entries[a], &entries[b]);
            b.folder
                .cmp(&a.folder)
                .then_with(|| match sort {
                    SavedSqlSort::Modified if !a.folder => b.modified.cmp(&a.modified),
                    _ => Ordering::Equal,
                })
                .then_with(|| compare_names(&a.relative_path, &b.relative_path))
        });
    }
    let mut tree = SavedSqlTree {
        has_more: truncated,
        ..SavedSqlTree::default()
    };
    let (mut files, mut snippet_bytes) = (0usize, 0usize);
    let mut stack: Vec<usize> = children
        .get(Path::new(""))
        .map(|list| list.iter().rev().copied().collect())
        .unwrap_or_default();
    while let Some(index) = stack.pop() {
        let entry = &entries[index];
        if !entry.folder {
            if files == MAX_SAVED_SQL_DOCUMENTS {
                tree.has_more = true;
                break;
            }
            files += 1;
        }
        let shown = entry.relative_path.to_string_lossy().into_owned();
        let mut keyword = None;
        match &entry.head {
            Some(Ok(text)) => match split_keyword(text) {
                Some((word, _)) if !valid_keyword(word) => {
                    tree.errors.push(format!("{shown}: {KEYWORD_RULE}"));
                }
                Some((word, sql)) if snippet_bytes + sql.len() > MAX_SNIPPET_TOTAL_BYTES => {
                    keyword = Some(word.to_owned());
                    tree.errors.push(format!(
                        "{shown}: Keyword snippets are limited to {} MiB in total.",
                        MAX_SNIPPET_TOTAL_BYTES >> 20
                    ));
                }
                Some((word, sql)) => {
                    snippet_bytes += sql.len();
                    keyword = Some(word.to_owned());
                    tree.snippets.push(SavedSqlSnippet {
                        keyword: word.to_owned(),
                        sql: sql.to_owned(),
                    });
                }
                None => {}
            },
            Some(Err(error)) => tree.errors.push(format!("{shown}: {error}")),
            None => {}
        }
        tree.nodes.push(SavedSqlNode {
            path: root.join(&entry.relative_path),
            relative_path: entry.relative_path.clone(),
            folder: entry.folder,
            keyword,
        });
        if entry.folder
            && let Some(list) = children.get(&entry.relative_path)
        {
            stack.extend(list.iter().rev().copied());
        }
    }
    Ok(tree)
}

pub fn set_saved_sql_keyword(root: &Path, path: &Path, keyword: &str) -> Result<(), String> {
    let keyword = keyword.trim();
    if !keyword.is_empty() && !valid_keyword(keyword) {
        return Err(KEYWORD_RULE.into());
    }
    let bytes = read_saved_sql_document(root, path).map_err(|error| error.to_string())?;
    let text = String::from_utf8(bytes).map_err(|_| "The file is not UTF-8 text.".to_owned())?;
    let body = split_keyword(&text).map_or(text.as_str(), |(_, rest)| rest);
    let updated = if keyword.is_empty() {
        body.to_owned()
    } else {
        format!("{KEYWORD_PREFIX} {keyword}\n{body}")
    };
    write_sql_document(path, updated.as_bytes()).map_err(|error| error.to_string())
}

pub fn create_saved_sql_folder(root: &Path, parent: &Path, name: &str) -> Result<PathBuf, String> {
    let name = name.trim();
    if name.is_empty()
        || name.starts_with('.')
        || name.contains(['/', '\\', '\0'])
        || Path::new(name).components().count() != 1
    {
        return Err(FOLDER_NAME_RULE.into());
    }
    if parent
        .components()
        .any(|component| !matches!(component, Component::Normal(_)))
    {
        return Err("The parent folder is not in the saved SQL folder.".into());
    }
    let mut directory = open_saved_root(root).map_err(|error| error.to_string())?;
    for component in parent.components() {
        directory = directory
            .open_dir_nofollow(component.as_os_str())
            .map_err(|_| "The parent folder is not in the saved SQL folder.".to_owned())?;
    }
    match directory.create_dir(name) {
        Ok(()) => Ok(root.join(parent).join(name)),
        Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => {
            Err("A folder or file with this name exists.".into())
        }
        Err(error) => Err(error.to_string()),
    }
}
