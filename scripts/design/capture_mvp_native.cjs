#!/usr/bin/env node
// Export production-backed offline Qt specimens; no app data or profiles are read.
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { execFileSync } = require('node:child_process');
const { createHash } = require('node:crypto');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const executable = path.resolve(process.argv[2] || path.join(root, 'build/ci/native/choscordb-component-gallery'));
const output = path.resolve(process.argv[3] || path.join(root, 'docs/design/mvp-native'));
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'choscordb-mvp-gallery-'));
const env = { ...process.env, QT_QPA_PLATFORM: process.env.QT_QPA_PLATFORM || 'cocoa', XDG_CONFIG_HOME: path.join(scratch, 'config'), XDG_DATA_HOME: path.join(scratch, 'data') };
const run = args => execFileSync(executable, args, { cwd: root, env, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
fs.mkdirSync(output, { recursive: true });
const manifest = { schema: 1, capturedAt: new Date().toISOString(), gitRef: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim(), executable, executableSha256: hash(fs.readFileSync(executable)), procedure: 'node scripts/design/capture_mvp_native.cjs [gallery-executable] [output-directory]', platform: env.QT_QPA_PLATFORM, rendering: 'Production QWidget rendering into a logical-pixel QImage; actual popup/modal widget contents. App client/specimen rectangle excludes the native shell. This is not an OS framebuffer screenshot or screen acceptance.', sources: [], captures: [] };
for (const folder of ['desktop/design_system', 'desktop/resources/icons', 'desktop/widgets']) {
  for (const name of fs.readdirSync(path.join(root, folder)).sort()) {
    const filename = path.join(folder, name);
    if (fs.statSync(path.join(root, filename)).isFile()) manifest.sources.push({ path: filename, sha256: hash(fs.readFileSync(path.join(root, filename))) });
  }
}
try {
  const ids = run(['--list']).trim().split('\n');
  assert.ok(ids.includes('buttons') && ids.includes('dialogs'));
  for (const [width, height] of [[1280, 900], [960, 640]]) {
    for (const theme of ['light', 'dark']) {
      for (const specimen of ids) {
        const name = `${theme}-${width}x${height}-${specimen}.png`;
        const file = path.join(output, name);
        const args = ['--export', file, '--specimen', specimen, '--theme', theme, '--width', String(width), '--height', String(height)];
        run(args);
        const bytes = fs.readFileSync(file);
        assert.equal(bytes.readUInt32BE(16), width);
        assert.equal(bytes.readUInt32BE(20), height);
        const metadata = JSON.parse(fs.readFileSync(file + '.json', 'utf8'));
        assert.equal(metadata.platform, env.QT_QPA_PLATFORM);
        assert.equal(metadata.themes.toLowerCase(), theme);
        manifest.captures.push({ specimen, theme, viewport: { width, height }, file: name, metadata: name + '.json', sha256: hash(bytes), args });
        console.log(name);
      }
    }
  }
} finally {
  fs.writeFileSync(path.join(output, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
  fs.rmSync(scratch, { recursive: true, force: true });
}
