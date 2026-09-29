use crate::{
    UpdateError, UpdateRecord, parse_signed_update_metadata, verify_update_file_with_cancel,
};
#[cfg(target_os = "linux")]
use std::path::Component;
use std::{
    io::Write,
    path::{Path, PathBuf},
    time::Duration,
};
use tokio_util::sync::CancellationToken;

const MAX_FEED_BYTES: usize = 32_768;
const MAX_PACKAGE_REDIRECTS: usize = 10;

#[derive(Debug, thiserror::Error)]
pub enum NetworkError {
    #[error("Update feed URL is not an approved HTTPS endpoint")]
    FeedPolicy,
    #[error("The update feed is unavailable. Please try again later.")]
    FeedUnavailable,
    #[error("Download failed. Please try again.")]
    DownloadFailed,
    #[error("Update download was cancelled")]
    Cancelled,
    #[error("The downloaded update failed integrity verification.")]
    Integrity,
    #[error("This computer cannot safely stage the update")]
    StagingUnavailable,
    #[error("{0}")]
    Metadata(#[from] UpdateError),
}

#[derive(Clone)]
pub struct CheckedUpdate {
    record: UpdateRecord,
    envelope: Vec<u8>,
    platform: String,
}

impl CheckedUpdate {
    pub fn authenticate(
        envelope: &[u8],
        public_key: &[u8],
        current_version: &str,
        platform: &str,
        arch: &str,
        repository: &str,
    ) -> Result<Option<Self>, UpdateError> {
        let record = parse_signed_update_metadata(
            envelope,
            public_key,
            current_version,
            platform,
            arch,
            repository,
        )?;
        Ok(record.map(|record| Self {
            record,
            envelope: envelope.to_vec(),
            platform: platform.to_owned(),
        }))
    }

    pub fn record(&self) -> &UpdateRecord {
        &self.record
    }
    pub fn envelope(&self) -> &[u8] {
        &self.envelope
    }
    pub fn platform(&self) -> &str {
        &self.platform
    }
}

pub struct StagedUpdate {
    path: tempfile::TempPath,
}

enum StagingKind {
    #[cfg(target_os = "linux")]
    LinuxAppImage,
    WindowsTemp,
    #[cfg(test)]
    Fixture,
}

pub struct StagingLocation {
    directory: PathBuf,
    kind: StagingKind,
}

impl StagingLocation {
    #[cfg(target_os = "linux")]
    pub fn for_linux_appimage(appimage: &Path, invoked: &Path) -> Result<Self, NetworkError> {
        use std::os::unix::fs::PermissionsExt;
        let target = absolute_clean(appimage).ok_or(NetworkError::StagingUnavailable)?;
        if !appimage.is_absolute()
            || absolute_clean(invoked).as_ref() != Some(&target)
            || !target.as_os_str().to_string_lossy().ends_with(".AppImage")
        {
            return Err(NetworkError::StagingUnavailable);
        }
        let metadata =
            std::fs::symlink_metadata(&target).map_err(|_| NetworkError::StagingUnavailable)?;
        if !metadata.is_file()
            || metadata.file_type().is_symlink()
            || metadata.permissions().mode() & 0o111 == 0
            || target.canonicalize().ok().as_ref() != Some(&target)
        {
            return Err(NetworkError::StagingUnavailable);
        }
        let directory = target
            .parent()
            .ok_or(NetworkError::StagingUnavailable)?
            .to_path_buf();
        Ok(Self {
            directory,
            kind: StagingKind::LinuxAppImage,
        })
    }

    pub fn for_windows_temp() -> Self {
        Self {
            directory: std::env::temp_dir(),
            kind: StagingKind::WindowsTemp,
        }
    }

    #[cfg(test)]
    fn fixture(directory: &Path) -> Self {
        Self {
            directory: directory.to_path_buf(),
            kind: StagingKind::Fixture,
        }
    }
}

impl StagedUpdate {
    pub fn path(&self) -> &Path {
        self.path.as_ref()
    }

    pub(crate) fn into_temp_path(self) -> tempfile::TempPath {
        self.path
    }

    #[cfg(all(test, any(target_os = "linux", windows)))]
    pub(crate) fn fixture(path: tempfile::TempPath) -> Self {
        Self { path }
    }
}

pub struct UpdateNetworkService {
    client: reqwest::Client,
    feed_url: url::Url,
    package_override: Option<url::Url>,
    public_key: Vec<u8>,
    platform: String,
    arch: String,
    repository: String,
    failure_feed_fixture: bool,
}

impl UpdateNetworkService {
    pub fn new_production(
        feed_base: &str,
        public_key: &[u8],
        platform: &str,
        arch: &str,
        repository: &str,
    ) -> Result<Self, NetworkError> {
        let feed_base = url::Url::parse(feed_base).map_err(|_| NetworkError::FeedPolicy)?;
        let (owner, name) = repository.split_once('/').ok_or(NetworkError::FeedPolicy)?;
        let valid_name = |part: &str| {
            !part.is_empty()
                && part
                    .bytes()
                    .all(|byte| byte.is_ascii_alphanumeric() || b"_.-".contains(&byte))
        };
        if !valid_name(owner) || !valid_name(name) {
            return Err(NetworkError::FeedPolicy);
        }
        let canonical_base = format!(
            "https://{}.github.io/{}/updates",
            owner.to_ascii_lowercase(),
            name.to_ascii_lowercase()
        );
        if feed_base.scheme() != "https"
            || feed_base.host_str().is_none()
            || feed_base.username() != ""
            || feed_base.password().is_some()
            || feed_base.query().is_some()
            || feed_base.fragment().is_some()
            || feed_base
                .as_str()
                .trim_end_matches('/')
                .to_ascii_lowercase()
                != canonical_base
        {
            return Err(NetworkError::FeedPolicy);
        }
        let feed_name = match (platform, arch) {
            ("windows", "x64") => "windows-x64.json",
            ("linux", "x86_64") => "linux-x86_64.json",
            _ => return Err(NetworkError::FeedPolicy),
        };
        let feed_url = url::Url::parse(&format!(
            "{}/{}",
            feed_base.as_str().trim_end_matches('/'),
            feed_name
        ))
        .map_err(|_| NetworkError::FeedPolicy)?;
        Ok(Self {
            client: client()?,
            feed_url,
            package_override: None,
            public_key: public_key.into(),
            platform: platform.into(),
            arch: arch.into(),
            repository: repository.into(),
            failure_feed_fixture: false,
        })
    }

    // The native menu test checks that an unavailable feed does not grant consent.
    // This endpoint can only fail a check and can never authorize a package.
    pub fn new_failure_feed_fixture(feed_url: &str) -> Result<Self, NetworkError> {
        let feed_url = url::Url::parse(feed_url).map_err(|_| NetworkError::FeedPolicy)?;
        let loopback = matches!(feed_url.host_str(), Some("127.0.0.1" | "[::1]" | "::1"));
        if feed_url.scheme() != "http"
            || !loopback
            || feed_url.username() != ""
            || feed_url.password().is_some()
            || feed_url.fragment().is_some()
        {
            return Err(NetworkError::FeedPolicy);
        }
        Ok(Self {
            client: client()?,
            feed_url,
            package_override: None,
            public_key: Vec::new(),
            platform: String::new(),
            arch: String::new(),
            repository: String::new(),
            failure_feed_fixture: true,
        })
    }

    #[cfg(test)]
    fn fixture(
        feed_url: url::Url,
        package_url: url::Url,
        public_key: &[u8],
        platform: &str,
        arch: &str,
        repository: &str,
    ) -> Self {
        Self {
            client: client().expect("HTTP fixture client"),
            feed_url,
            package_override: Some(package_url),
            public_key: public_key.into(),
            platform: platform.into(),
            arch: arch.into(),
            repository: repository.into(),
            failure_feed_fixture: false,
        }
    }

    pub async fn check(
        &self,
        current_version: &str,
        cancel: &CancellationToken,
    ) -> Result<Option<CheckedUpdate>, NetworkError> {
        if cancel.is_cancelled() {
            return Err(NetworkError::Cancelled);
        }
        let request = self
            .client
            .get(self.feed_url.clone())
            .timeout(Duration::from_secs(15));
        let mut response = tokio::select! {
            _ = cancel.cancelled() => return Err(NetworkError::Cancelled),
            result = request.send() => result.map_err(|_| NetworkError::FeedUnavailable)?,
        };
        if response.status() != reqwest::StatusCode::OK || self.failure_feed_fixture {
            return Err(NetworkError::FeedUnavailable);
        }
        let mut envelope = Vec::new();
        loop {
            let chunk = tokio::select! {
                _ = cancel.cancelled() => return Err(NetworkError::Cancelled),
                result = response.chunk() => result.map_err(|_| NetworkError::FeedUnavailable)?,
            };
            let Some(chunk) = chunk else {
                break;
            };
            if chunk.len() > MAX_FEED_BYTES - envelope.len() {
                return Err(NetworkError::FeedUnavailable);
            }
            envelope.extend_from_slice(&chunk);
        }
        let checked = CheckedUpdate::authenticate(
            &envelope,
            &self.public_key,
            current_version,
            &self.platform,
            &self.arch,
            &self.repository,
        )?;
        Ok(checked)
    }

    pub async fn download(
        &self,
        update: &CheckedUpdate,
        staging: &StagingLocation,
        cancel: &CancellationToken,
        mut progress: impl FnMut(u64, u64),
    ) -> Result<StagedUpdate, NetworkError> {
        if cancel.is_cancelled() {
            return Err(NetworkError::Cancelled);
        }
        let stage_matches = matches!(
            (&self.platform[..], &staging.kind),
            ("windows", StagingKind::WindowsTemp)
        );
        #[cfg(target_os = "linux")]
        let stage_matches = stage_matches
            || matches!(
                (&self.platform[..], &staging.kind),
                ("linux", StagingKind::LinuxAppImage)
            );
        #[cfg(test)]
        let stage_matches = stage_matches || matches!(staging.kind, StagingKind::Fixture);
        if !stage_matches {
            return Err(NetworkError::StagingUnavailable);
        }
        let verified = parse_signed_update_metadata(
            &update.envelope,
            &self.public_key,
            "0.0.0",
            &self.platform,
            &self.arch,
            &self.repository,
        )?;
        if verified.as_ref() != Some(&update.record) {
            return Err(NetworkError::DownloadFailed);
        }
        let original =
            url::Url::parse(&update.record.url).map_err(|_| NetworkError::DownloadFailed)?;
        if !allowed_package_url(&original) {
            return Err(NetworkError::DownloadFailed);
        }
        let mut url = self.package_override.clone().unwrap_or(original);
        let (prefix, suffix) = if self.platform == "linux" {
            (".ChoscorDB-update-", ".AppImage")
        } else {
            ("ChoscorDB-update-", ".exe")
        };
        let mut file = tempfile::Builder::new()
            .prefix(prefix)
            .suffix(suffix)
            .tempfile_in(&staging.directory)
            .map_err(|_| NetworkError::StagingUnavailable)?;
        let mut response = None;
        for redirects in 0..=MAX_PACKAGE_REDIRECTS {
            let request = self.client.get(url.clone());
            let next = tokio::select! {
                _ = cancel.cancelled() => return Err(NetworkError::Cancelled),
                result = request.send() => result.map_err(|_| NetworkError::DownloadFailed)?,
            };
            if !next.status().is_redirection() {
                response = Some(next);
                break;
            }
            if redirects == MAX_PACKAGE_REDIRECTS {
                return Err(NetworkError::DownloadFailed);
            }
            let location = next
                .headers()
                .get(reqwest::header::LOCATION)
                .and_then(|value| value.to_str().ok())
                .ok_or(NetworkError::DownloadFailed)?;
            url = url
                .join(location)
                .map_err(|_| NetworkError::DownloadFailed)?;
            if !allowed_package_url(&url) {
                return Err(NetworkError::DownloadFailed);
            }
        }
        let mut response = response.ok_or(NetworkError::DownloadFailed)?;
        if response.status() != reqwest::StatusCode::OK {
            return Err(NetworkError::DownloadFailed);
        }
        let mut received = 0u64;
        loop {
            let chunk = tokio::select! {
                _ = cancel.cancelled() => return Err(NetworkError::Cancelled),
                result = response.chunk() => result.map_err(|_| NetworkError::DownloadFailed)?,
            };
            let Some(chunk) = chunk else {
                break;
            };
            received = received
                .checked_add(chunk.len() as u64)
                .ok_or(NetworkError::DownloadFailed)?;
            if received > update.record.size {
                return Err(NetworkError::DownloadFailed);
            }
            file.write_all(&chunk)
                .map_err(|_| NetworkError::DownloadFailed)?;
            progress(received, update.record.size);
        }
        file.flush().map_err(|_| NetworkError::DownloadFailed)?;
        let path = file.into_temp_path();
        if cancel.is_cancelled() {
            return Err(NetworkError::Cancelled);
        }
        match verify_update_file_with_cancel(path.as_ref(), &update.record, || {
            cancel.is_cancelled()
        }) {
            Ok(()) => {}
            Err(UpdateError::VerificationCancelled) => return Err(NetworkError::Cancelled),
            Err(_) => return Err(NetworkError::Integrity),
        }
        if cancel.is_cancelled() {
            return Err(NetworkError::Cancelled);
        }
        Ok(StagedUpdate { path })
    }
}

#[cfg(target_os = "linux")]
fn absolute_clean(path: &Path) -> Option<PathBuf> {
    let absolute = if path.is_absolute() {
        path.to_path_buf()
    } else {
        std::env::current_dir().ok()?.join(path)
    };
    let mut clean = PathBuf::new();
    for component in absolute.components() {
        match component {
            Component::CurDir => {}
            Component::ParentDir => {
                clean.pop();
            }
            Component::Normal(part) => clean.push(part),
            Component::RootDir => clean.push(component.as_os_str()),
            Component::Prefix(_) => clean.push(component.as_os_str()),
        }
    }
    Some(clean)
}

fn client() -> Result<reqwest::Client, NetworkError> {
    reqwest::Client::builder()
        .redirect(reqwest::redirect::Policy::none())
        .connect_timeout(Duration::from_secs(15))
        .read_timeout(Duration::from_secs(30))
        .build()
        .map_err(|_| NetworkError::FeedUnavailable)
}

fn allowed_package_url(url: &url::Url) -> bool {
    url.scheme() == "https"
        && url.username().is_empty()
        && url.password().is_none()
        && matches!(
            url.host_str(),
            Some("github.com" | "release-assets.githubusercontent.com")
        )
}

#[cfg(test)]
mod tests {
    use super::*;
    use base64::Engine as _;
    use ed25519_dalek::{Signer as _, SigningKey};
    use serde_json::json;
    use tokio::{
        io::{AsyncReadExt, AsyncWriteExt},
        net::TcpListener,
        sync::oneshot,
    };

    async fn fixture_server(
        feed: Vec<u8>,
        package: Vec<u8>,
    ) -> (url::Url, tokio::task::JoinHandle<()>) {
        let header = format!(
            "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
            package.len()
        );
        let mut response = header.into_bytes();
        response.extend(package);
        fixture_server_with_package_response(feed, response).await
    }

    async fn fixture_server_with_package_response(
        feed: Vec<u8>,
        package_response: Vec<u8>,
    ) -> (url::Url, tokio::task::JoinHandle<()>) {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let url = url::Url::parse(&format!("http://{}/", listener.local_addr().unwrap())).unwrap();
        let task = tokio::spawn(async move {
            for _ in 0..2 {
                let (mut stream, _) = listener.accept().await.unwrap();
                let mut buffer = [0u8; 4096];
                let size = stream.read(&mut buffer).await.unwrap();
                let request = std::str::from_utf8(&buffer[..size]).unwrap();
                if request.starts_with("GET /feed ") {
                    let header = format!(
                        "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                        feed.len()
                    );
                    stream.write_all(header.as_bytes()).await.unwrap();
                    stream.write_all(&feed).await.unwrap();
                } else {
                    stream.write_all(&package_response).await.unwrap();
                }
            }
        });
        (url, task)
    }

    fn signed_feed() -> (Vec<u8>, [u8; 32]) {
        signed_feed_with_url(
            "https://github.com/choscor/choscordb/releases/download/v1.2.4/ChoscorDB-1.2.4-linux-x86_64.AppImage",
        )
    }

    fn signed_feed_with_url(package_url: &str) -> (Vec<u8>, [u8; 32]) {
        let signing = SigningKey::from_bytes(&[7u8; 32]);
        let payload = serde_json::to_vec(&json!({
            "version": "1.2.4", "platform": "linux", "arch": "x86_64",
            "url": package_url,
            "size": 4,
            "sha256": "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589",
            "notes": "Stable improvements"
        }))
        .unwrap();
        let envelope = serde_json::to_vec(&json!({
            "key_id": "windows-linux-v1",
            "payload": base64::engine::general_purpose::STANDARD.encode(&payload),
            "signature": base64::engine::general_purpose::STANDARD.encode(signing.sign(&payload).to_bytes()),
        })).unwrap();
        (envelope, signing.verifying_key().to_bytes())
    }

    #[tokio::test]
    async fn fetches_signed_feed_and_stages_verified_package() {
        let (feed, key) = signed_feed();
        let (url, server) = fixture_server(feed, b"abcd".to_vec()).await;
        let service = UpdateNetworkService::fixture(
            url.join("feed").unwrap(),
            url.join("package").unwrap(),
            &key,
            "linux",
            "x86_64",
            "choscor/choscordb",
        );
        let cancel = CancellationToken::new();
        let update = service.check("1.2.3", &cancel).await.unwrap().unwrap();
        assert_eq!(update.record().version, "1.2.4");
        let directory = tempfile::tempdir().unwrap();
        let staging = StagingLocation::fixture(directory.path());
        let mut observed = Vec::new();
        let staged = service
            .download(&update, &staging, &cancel, |done, total| {
                observed.push((done, total))
            })
            .await
            .unwrap();
        assert_eq!(std::fs::read(staged.path()).unwrap(), b"abcd");
        let name = staged.path().file_name().unwrap().to_str().unwrap();
        assert!(name.starts_with(".ChoscorDB-update-") && name.ends_with(".AppImage"));
        assert!(observed.contains(&(4, 4)));
        drop(staged);
        assert_eq!(std::fs::read_dir(directory.path()).unwrap().count(), 0);
        server.await.unwrap();
    }

    #[cfg(target_os = "linux")]
    #[tokio::test]
    async fn linux_appimage_staging_uses_target_directory_with_verified_bytes() {
        use std::os::unix::fs::PermissionsExt;
        let (feed, key) = signed_feed();
        let (url, server) = fixture_server(feed, b"abcd".to_vec()).await;
        let service = UpdateNetworkService::fixture(
            url.join("feed").unwrap(),
            url.join("package").unwrap(),
            &key,
            "linux",
            "x86_64",
            "choscor/choscordb",
        );
        let directory = tempfile::tempdir().unwrap();
        let directory = directory.path().canonicalize().unwrap();
        let target = directory.join("ChoscorDB.AppImage");
        std::fs::write(&target, b"current appimage").unwrap();
        std::fs::set_permissions(&target, std::fs::Permissions::from_mode(0o755)).unwrap();
        let staging = StagingLocation::for_linux_appimage(&target, &target).unwrap();
        let checked = service
            .check("1.2.3", &CancellationToken::new())
            .await
            .unwrap()
            .unwrap();
        let staged = service
            .download(&checked, &staging, &CancellationToken::new(), |_, _| {})
            .await
            .unwrap();
        assert_eq!(staged.path().parent(), target.parent());
        assert_eq!(std::fs::read(staged.path()).unwrap(), b"abcd");
        assert_eq!(std::fs::read(&target).unwrap(), b"current appimage");
        drop(staged);
        assert_eq!(std::fs::read_dir(&directory).unwrap().count(), 1);
        server.await.unwrap();
    }

    #[test]
    fn production_rejects_http_feed_and_non_github_package_redirects() {
        let (_, key) = signed_feed();
        assert!(matches!(
            UpdateNetworkService::new_production(
                "http://127.0.0.1:1234/feed",
                &key,
                "linux",
                "x86_64",
                "choscor/choscordb"
            ),
            Err(NetworkError::FeedPolicy)
        ));
        assert!(matches!(
            UpdateNetworkService::new_production(
                "https://attacker.github.io/updates",
                &key,
                "linux",
                "x86_64",
                "choscor/choscordb"
            ),
            Err(NetworkError::FeedPolicy)
        ));
        let service = UpdateNetworkService::new_production(
            "https://choscor.github.io/choscordb/updates",
            &key,
            "linux",
            "x86_64",
            "choscor/choscordb",
        )
        .unwrap();
        assert_eq!(
            service.feed_url.as_str(),
            "https://choscor.github.io/choscordb/updates/linux-x86_64.json"
        );
        assert!(allowed_package_url(
            &url::Url::parse("https://release-assets.githubusercontent.com/asset").unwrap()
        ));
        assert!(!allowed_package_url(
            &url::Url::parse("http://github.com/asset").unwrap()
        ));
        assert!(!allowed_package_url(
            &url::Url::parse("https://github.com.attacker.invalid/asset").unwrap()
        ));
    }

    #[tokio::test]
    async fn cancelled_check_does_not_start_network_request() {
        let (_, key) = signed_feed();
        let service = UpdateNetworkService::new_production(
            "https://choscor.github.io/choscordb/updates",
            &key,
            "linux",
            "x86_64",
            "choscor/choscordb",
        )
        .unwrap();
        let cancel = CancellationToken::new();
        cancel.cancel();
        assert!(matches!(
            service.check("1.2.3", &cancel).await,
            Err(NetworkError::Cancelled)
        ));
    }

    #[tokio::test]
    async fn native_menu_failure_fixture_cannot_authorize_even_a_valid_signed_feed() {
        let (feed, _) = signed_feed();
        let (url, server) = fixture_server(feed, b"abcd".to_vec()).await;
        assert!(matches!(
            UpdateNetworkService::new_failure_feed_fixture("http://example.com/feed"),
            Err(NetworkError::FeedPolicy)
        ));
        let service =
            UpdateNetworkService::new_failure_feed_fixture(url.join("feed").unwrap().as_str())
                .unwrap();
        assert!(matches!(
            service.check("1.2.3", &CancellationToken::new()).await,
            Err(NetworkError::FeedUnavailable)
        ));
        server.abort();
    }

    #[cfg(target_os = "linux")]
    #[test]
    fn linux_staging_requires_direct_executable_appimage_path() {
        use std::os::unix::fs::{PermissionsExt, symlink};

        let directory = tempfile::tempdir().unwrap();
        let directory = directory.path().canonicalize().unwrap();
        let appimage = directory.join("ChoscorDB.AppImage");
        std::fs::write(&appimage, b"appimage").unwrap();
        std::fs::set_permissions(&appimage, std::fs::Permissions::from_mode(0o755)).unwrap();
        assert!(StagingLocation::for_linux_appimage(&appimage, &appimage).is_ok());
        assert!(
            StagingLocation::for_linux_appimage(Path::new("ChoscorDB.AppImage"), &appimage)
                .is_err()
        );
        let alias = directory.join("alias.AppImage");
        symlink(&appimage, &alias).unwrap();
        assert!(StagingLocation::for_linux_appimage(&alias, &alias).is_err());
        assert!(StagingLocation::for_linux_appimage(&appimage, &alias).is_err());
        std::fs::set_permissions(&appimage, std::fs::Permissions::from_mode(0o644)).unwrap();
        assert!(StagingLocation::for_linux_appimage(&appimage, &appimage).is_err());
    }

    #[tokio::test]
    async fn oversized_feed_and_bad_package_bytes_fail_without_leaving_staged_files() {
        let (_, key) = signed_feed();
        let (url, server) = fixture_server(vec![b'x'; 32_769], b"abcd".to_vec()).await;
        let service = UpdateNetworkService::fixture(
            url.join("feed").unwrap(),
            url.join("package").unwrap(),
            &key,
            "linux",
            "x86_64",
            "choscor/choscordb",
        );
        assert!(matches!(
            service.check("1.2.3", &CancellationToken::new()).await,
            Err(NetworkError::FeedUnavailable)
        ));
        server.abort();

        let (feed, key) = signed_feed();
        let (url, server) = fixture_server(feed, b"abce".to_vec()).await;
        let service = UpdateNetworkService::fixture(
            url.join("feed").unwrap(),
            url.join("package").unwrap(),
            &key,
            "linux",
            "x86_64",
            "choscor/choscordb",
        );
        let update = service
            .check("1.2.3", &CancellationToken::new())
            .await
            .unwrap()
            .unwrap();
        let directory = tempfile::tempdir().unwrap();
        let staging = StagingLocation::fixture(directory.path());
        assert!(matches!(
            service
                .download(&update, &staging, &CancellationToken::new(), |_, _| {})
                .await,
            Err(NetworkError::Integrity)
        ));
        assert_eq!(std::fs::read_dir(directory.path()).unwrap().count(), 0);
        server.await.unwrap();

        let (feed, key) = signed_feed();
        let (url, server) = fixture_server(feed, b"abcde".to_vec()).await;
        let service = UpdateNetworkService::fixture(
            url.join("feed").unwrap(),
            url.join("package").unwrap(),
            &key,
            "linux",
            "x86_64",
            "choscor/choscordb",
        );
        let update = service
            .check("1.2.3", &CancellationToken::new())
            .await
            .unwrap()
            .unwrap();
        assert!(matches!(
            service
                .download(&update, &staging, &CancellationToken::new(), |_, _| {})
                .await,
            Err(NetworkError::DownloadFailed)
        ));
        assert_eq!(std::fs::read_dir(directory.path()).unwrap().count(), 0);
        server.await.unwrap();

        let (feed, key) = signed_feed();
        let reply = b"HTTP/1.1 302 Found\r\nLocation: http://github.com/unsafe\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".to_vec();
        let (url, server) = fixture_server_with_package_response(feed, reply).await;
        let service = UpdateNetworkService::fixture(
            url.join("feed").unwrap(),
            url.join("package").unwrap(),
            &key,
            "linux",
            "x86_64",
            "choscor/choscordb",
        );
        let update = service
            .check("1.2.3", &CancellationToken::new())
            .await
            .unwrap()
            .unwrap();
        assert!(matches!(
            service
                .download(&update, &staging, &CancellationToken::new(), |_, _| {})
                .await,
            Err(NetworkError::DownloadFailed)
        ));
        assert_eq!(std::fs::read_dir(directory.path()).unwrap().count(), 0);
        server.await.unwrap();
    }

    #[tokio::test]
    async fn fixture_cannot_authorize_local_signed_package_url_or_feed_redirect() {
        let (feed, key) = signed_feed_with_url("http://127.0.0.1/package");
        let (url, server) = fixture_server(feed, b"abcd".to_vec()).await;
        let service = UpdateNetworkService::fixture(
            url.join("feed").unwrap(),
            url.join("package").unwrap(),
            &key,
            "linux",
            "x86_64",
            "choscor/choscordb",
        );
        assert!(matches!(
            service.check("1.2.3", &CancellationToken::new()).await,
            Err(NetworkError::Metadata(UpdateError::UnexpectedPackageUrl))
        ));
        server.abort();

        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let url = url::Url::parse(&format!("http://{}/", listener.local_addr().unwrap())).unwrap();
        let server = tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            let mut request = [0u8; 4096];
            assert!(stream.read(&mut request).await.unwrap() > 0);
            stream.write_all(b"HTTP/1.1 302 Found\r\nLocation: https://github.com/other\r\nContent-Length: 0\r\nConnection: close\r\n\r\n").await.unwrap();
        });
        let (_, key) = signed_feed();
        let service = UpdateNetworkService::fixture(
            url.join("feed").unwrap(),
            url.join("package").unwrap(),
            &key,
            "linux",
            "x86_64",
            "choscor/choscordb",
        );
        assert!(matches!(
            service.check("1.2.3", &CancellationToken::new()).await,
            Err(NetworkError::FeedUnavailable)
        ));
        server.await.unwrap();
    }

    #[tokio::test]
    async fn cancellation_during_package_stream_removes_partial_stage_promptly() {
        let (feed, key) = signed_feed();
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let url = url::Url::parse(&format!("http://{}/", listener.local_addr().unwrap())).unwrap();
        let (partial_tx, partial_rx) = oneshot::channel();
        let server = tokio::spawn(async move {
            let (mut stream, _) = listener.accept().await.unwrap();
            let mut request = [0u8; 4096];
            assert!(stream.read(&mut request).await.unwrap() > 0);
            let header = format!(
                "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                feed.len()
            );
            stream.write_all(header.as_bytes()).await.unwrap();
            stream.write_all(&feed).await.unwrap();
            drop(stream);
            let (mut stream, _) = listener.accept().await.unwrap();
            assert!(stream.read(&mut request).await.unwrap() > 0);
            stream
                .write_all(b"HTTP/1.1 200 OK\r\nContent-Length: 4\r\nConnection: close\r\n\r\nab")
                .await
                .unwrap();
            partial_tx.send(()).unwrap();
            tokio::time::sleep(Duration::from_secs(5)).await;
        });
        let service = UpdateNetworkService::fixture(
            url.join("feed").unwrap(),
            url.join("package").unwrap(),
            &key,
            "linux",
            "x86_64",
            "choscor/choscordb",
        );
        let cancel = CancellationToken::new();
        let update = service.check("1.2.3", &cancel).await.unwrap().unwrap();
        let directory = tempfile::tempdir().unwrap();
        let staging = StagingLocation::fixture(directory.path());
        let download_cancel = cancel.clone();
        let job = tokio::spawn(async move {
            service
                .download(&update, &staging, &download_cancel, |_, _| {})
                .await
                .map(|_| ())
        });
        partial_rx.await.unwrap();
        cancel.cancel();
        let result = tokio::time::timeout(Duration::from_secs(1), job)
            .await
            .unwrap()
            .unwrap();
        assert!(matches!(result, Err(NetworkError::Cancelled)));
        assert_eq!(std::fs::read_dir(directory.path()).unwrap().count(), 0);
        server.abort();
    }
}
