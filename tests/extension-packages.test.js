const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const Zip = require('adm-zip');
const packages = require('../extensionPackages');
const { compareVersions } = require('../extensionStore');

const manifest = { name: 'Fixture extension', version: '1.0', manifest_version: 3 };
function zip(files) {
    const archive = new Zip();
    for (const [name, value] of Object.entries(files)) archive.addFile(name, Buffer.from(value));
    return archive.toBuffer();
}
function varint(n) {
    const bytes = [];
    do { const byte = n & 127; n = Math.floor(n / 128); bytes.push(byte | (n ? 128 : 0)); } while (n);
    return Buffer.from(bytes);
}
const field = (tag, value) => Buffer.concat([varint(tag * 8 + 2), varint(value.length), value]);
function crx(payload, version = 3, algorithm = 'rsa') {
    const keys = crypto.generateKeyPairSync(algorithm === 'rsa' ? 'rsa' : 'ec', algorithm === 'rsa' ? { modulusLength: 2048 } : { namedCurve: 'prime256v1' });
    const key = keys.publicKey.export({ type: 'spki', format: 'der' });
    const prefix = Buffer.alloc(version === 2 ? 16 : 12);
    prefix.write('Cr24'); prefix.writeUInt32LE(version, 4);
    if (version === 2) {
        const signature = crypto.sign('sha1', payload, keys.privateKey);
        prefix.writeUInt32LE(key.length, 8); prefix.writeUInt32LE(signature.length, 12);
        return { bytes: Buffer.concat([prefix, key, signature, payload]), key };
    }
    const signed = field(1, crypto.createHash('sha256').update(key).digest().subarray(0, 16));
    const size = Buffer.alloc(4); size.writeUInt32LE(signed.length);
    const signature = crypto.sign('sha256', Buffer.concat([Buffer.from('CRX3 SignedData\0'), size, signed, payload]), keys.privateKey);
    const header = Buffer.concat([field(algorithm === 'rsa' ? 2 : 3, Buffer.concat([field(1, key), field(2, signature)])), field(10000, signed)]);
    prefix.writeUInt32LE(header.length, 8);
    return { bytes: Buffer.concat([prefix, header, payload]), key };
}

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-extension-packages-'));
    let serial = 0;
    const unpack = (buffer, id) => packages.unpackPackage(buffer, path.join(temporary, String(serial++)), id);
    try {
        const payload = zip({ 'manifest.json': JSON.stringify(manifest), 'content.js': 'document.body.dataset.extension = "yes";' });
        assert.equal(unpack(payload).manifest.name, manifest.name);
        assert.equal(unpack(zip({ 'wrapper/manifest.json': JSON.stringify(manifest) })).manifest.name, manifest.name);
        assert.throws(() => unpack(zip({ 'one/manifest.json': JSON.stringify(manifest), 'two/manifest.json': JSON.stringify(manifest) })), /root or inside/);
        assert.throws(() => unpack(zip({ 'package.json': '{}' })), /root or inside/);
        assert.throws(() => unpack(zip({ 'manifest.json': JSON.stringify({ ...manifest, manifest_version: 4 }) })), /valid manifest/);
        const badPath = zip({ '12345678901': 'evil' });
        const unsafe = Buffer.from(badPath.toString('latin1').replaceAll('12345678901', '../evil.txt'), 'latin1');
        assert.throws(() => unpack(unsafe), /Unsafe/);
        const symlink = new Zip(); symlink.addFile('link', Buffer.from('/etc/passwd'), '', 0o120777 << 16);
        // Set the actual Unix type bits, independent of the ZIP creator platform.
        symlink.getEntries()[0].header.attr = (0o120777 << 16) >>> 0;
        assert.throws(() => unpack(symlink.toBuffer()), /symbolic/);
        const expanded = Buffer.from(payload);
        const central = expanded.indexOf(Buffer.from('504b0102', 'hex'));
        expanded.writeUInt32LE(300 * 1024 * 1024, central + 24);
        assert.throws(() => unpack(expanded), /256 MB/);
        for (const [version, algorithm] of [[2, 'rsa'], [3, 'rsa'], [3, 'ec']]) {
            const signed = crx(payload, version, algorithm);
            const id = packages.extensionId(signed.key);
            const result = unpack(signed.bytes, id);
            assert.equal(result.id, id);
            assert.equal(result.manifest.key, signed.key.toString('base64'));
            assert.throws(() => unpack(signed.bytes, 'a'.repeat(32)), /identity/);
            const corrupt = Buffer.from(signed.bytes); corrupt[corrupt.length - 1] ^= 1;
            assert.throws(() => unpack(corrupt, id), /signature/);
            for (const size of [0, 10, 18]) assert.throws(() => packages.parseCrx(signed.bytes.subarray(0, size)));
        }
        assert.throws(() => unpack(payload, 'a'.repeat(32)), /signed CRX/);
        const source = path.join(temporary, 'source'); fs.mkdirSync(source);
        fs.writeFileSync(path.join(source, 'manifest.json'), JSON.stringify(manifest));
        assert.equal(packages.copyUnpacked(source, path.join(temporary, 'copy')).manifest.version, manifest.version);
        fs.symlinkSync('/etc/passwd', path.join(source, 'link'));
        assert.throws(() => packages.copyUnpacked(source, path.join(temporary, 'copy2')), /symbolic/);
        const id = 'a'.repeat(32);
        assert.equal(packages.storeId(`https://chromewebstore.google.com/detail/name/${id}?hl=en`), id);
        for (const input of [`https://chromewebstore.google.com.evil/detail/${id}`, `http://chromewebstore.google.com/detail/${id}`, `file:///${id}`, id + '/../../x']) assert.throws(() => packages.storeId(input));
        let requested;
        const downloaded = await packages.downloadStorePackage(id, '150.0.0.0', async url => {
            requested = url; return new Response(Buffer.from('package'));
        });
        assert.equal(downloaded.toString(), 'package');
        assert.equal(requested.searchParams.get('x'), `id=${id}&uc`);
        assert.equal(requested.searchParams.get('prodversion'), '150.0.0.0');
        await assert.rejects(packages.downloadStorePackage(id, '150', async () => new Response(null, { status: 302, headers: { location: 'http://example.com/ext' } })), /Insecure/);
        await assert.rejects(packages.downloadStorePackage(id, '150', async () => new Response('', { status: 404 })), /404/);
        assert.equal(compareVersions('2.0', '1.99'), 1);
        assert.equal(compareVersions('1.0', '1.0.0'), 0);
        console.log('Extension packages: ZIP roots, traversal, symlinks, size limits, source copying, CRX2/3 signatures and identity, store URLs and downloads passed.');
    } finally { fs.rmSync(temporary, { recursive: true, force: true }); }
})().catch(error => { console.error(error); process.exitCode = 1; });
