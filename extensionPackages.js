// Package handling is independent of Electron so untrusted archives and signed
// CRX identity can be tested without loading any extension code.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const MAX_PACKAGE = 64 * 1024 * 1024;
const MAX_EXPANDED = 256 * 1024 * 1024;
const MAX_FILES = 20000;

function extensionId(key) {
    return [...crypto.createHash('sha256').update(key).digest('hex').slice(0, 32)]
        .map(c => String.fromCharCode(97 + parseInt(c, 16))).join('');
}

function storeId(input) {
    if (typeof input !== 'string') throw new Error('Enter a Chrome Web Store link or extension ID.');
    if (/^[a-p]{32}$/.test(input.trim())) return input.trim();
    try {
        const url = new URL(input.trim());
        if (url.protocol === 'https:' && url.hostname === 'chromewebstore.google.com') {
            const id = url.pathname.split('/').filter(Boolean).pop();
            if (/^[a-p]{32}$/.test(id) && url.pathname.startsWith('/detail/')) return id;
        }
    } catch {}
    throw new Error('Enter a Chrome Web Store detail link or a 32-character extension ID.');
}

// Minimal bounded protobuf reader for CRX3 length-delimited proof/header fields.
function fields(buffer) {
    let offset = 0;
    const result = [];
    const varint = () => {
        let value = 0;
        for (let shift = 0; shift < 35 && offset < buffer.length; shift += 7) {
            const byte = buffer[offset++];
            value += (byte & 127) * 2 ** shift;
            if (!(byte & 128)) return value;
        }
        throw new Error('Invalid CRX header.');
    };
    while (offset < buffer.length) {
        const tag = varint();
        const type = tag & 7;
        if (!(tag >>> 3)) throw new Error('Invalid CRX field.');
        if (type === 2) {
            const size = varint();
            if (offset + size > buffer.length) throw new Error('Truncated CRX header.');
            result.push([tag >>> 3, buffer.subarray(offset, offset + size)]);
            offset += size;
        } else if (type === 0) varint();
        else if (type === 1 || type === 5) {
            offset += type === 1 ? 8 : 4;
            if (offset > buffer.length) throw new Error('Truncated CRX field.');
        } else throw new Error('Unsupported CRX field.');
    }
    return result;
}

function parseCrx(buffer, expectedId) {
    if (buffer.length < 16 || buffer.toString('ascii', 0, 4) !== 'Cr24') throw new Error('Invalid CRX package.');
    const version = buffer.readUInt32LE(4);
    let key, contents;
    if (version === 2) {
        const keySize = buffer.readUInt32LE(8), signatureSize = buffer.readUInt32LE(12);
        const start = 16 + keySize + signatureSize;
        if (!keySize || !signatureSize || start >= buffer.length) throw new Error('Truncated CRX package.');
        key = buffer.subarray(16, 16 + keySize);
        contents = buffer.subarray(start);
        if (!crypto.verify('sha1', contents, { key, format: 'der', type: 'spki' }, buffer.subarray(16 + keySize, start))) {
            throw new Error('Invalid CRX signature.');
        }
    } else if (version === 3) {
        const size = buffer.readUInt32LE(8);
        if (size > 1024 * 1024 || 12 + size >= buffer.length) throw new Error('Invalid CRX header size.');
        const header = fields(buffer.subarray(12, 12 + size));
        const signed = header.find(([tag]) => tag === 10000)?.[1];
        if (!signed) throw new Error('Missing CRX signed header.');
        const idBytes = fields(signed).find(([tag]) => tag === 1)?.[1];
        if (idBytes?.length !== 16) throw new Error('Invalid CRX identity.');
        contents = buffer.subarray(12 + size);
        const length = Buffer.alloc(4); length.writeUInt32LE(signed.length);
        const signedBytes = Buffer.concat([Buffer.from('CRX3 SignedData\0'), length, signed, contents]);
        for (const [tag, proof] of header) {
            if (tag !== 2 && tag !== 3) continue;
            const parts = fields(proof);
            const publicKey = parts.find(([n]) => n === 1)?.[1];
            const signature = parts.find(([n]) => n === 2)?.[1];
            if (!publicKey || !signature || !crypto.createHash('sha256').update(publicKey).digest().subarray(0, 16).equals(idBytes)) continue;
            if (crypto.verify('sha256', signedBytes, { key: publicKey, format: 'der', type: 'spki' }, signature)) {
                key = publicKey; break;
            }
        }
        if (!key) throw new Error('Invalid CRX signature or identity.');
    } else throw new Error('Unsupported CRX version.');
    const id = extensionId(key);
    if (expectedId && id !== expectedId) throw new Error('Package identity does not match the requested extension.');
    return { contents, key: key.toString('base64'), id };
}

function validateManifest(directory) {
    const file = path.join(directory, 'manifest.json');
    if (fs.statSync(file).size > 1024 * 1024) throw new Error('Extension manifest is too large.');
    const manifest = JSON.parse(fs.readFileSync(file, 'utf8'));
    if (![2, 3].includes(manifest.manifest_version) || typeof manifest.name !== 'string' || !manifest.name.trim() ||
        typeof manifest.version !== 'string' || !/^\d+(\.\d+){0,3}$/.test(manifest.version) ||
        manifest.version.split('.').some(n => Number(n) > 65535) || manifest.app || manifest.theme) {
        throw new Error('Select a built Chrome extension with a valid manifest (not a theme, app or source repository).');
    }
    for (const field of ['permissions', 'host_permissions', 'optional_permissions', 'optional_host_permissions']) {
        if (manifest[field] !== undefined && (!Array.isArray(manifest[field]) || manifest[field].some(p => typeof p !== 'string'))) {
            throw new Error(`Invalid ${field} in extension manifest.`);
        }
    }
    if (manifest.default_locale !== undefined && !/^[a-zA-Z0-9_@-]+$/.test(manifest.default_locale)) throw new Error('Invalid extension locale.');
    return manifest;
}

function unpackPackage(buffer, destination, expectedId) {
    if (buffer.length > MAX_PACKAGE) throw new Error('Extension package exceeds 64 MB.');
    const crx = buffer.toString('ascii', 0, 4) === 'Cr24' ? parseCrx(buffer, expectedId) : null;
    if (expectedId && !crx) throw new Error('The store did not return a signed CRX package.');
    const Zip = require('adm-zip');
    const entries = new Zip(crx ? crx.contents : buffer).getEntries();
    if (entries.length > MAX_FILES) throw new Error('Too many files in extension package.');
    let total = 0;
    const names = new Set();
    for (const entry of entries) {
        const name = entry.entryName;
        if (!name || name.includes('\\') || name.includes('\0') || name.startsWith('/') ||
            /^[a-z]:/i.test(name) || name.split('/').some(p => p === '..' || p === '.') || names.has(name)) {
            throw new Error('Unsafe or duplicate path in extension package.');
        }
        names.add(name);
        if (((entry.header.attr >>> 16) & 0xf000) === 0xa000) throw new Error('Extension packages cannot contain symbolic links.');
        total += entry.header.size;
        if (total > MAX_EXPANDED) throw new Error('Extension expands beyond 256 MB.');
    }
    for (const entry of entries) {
        const target = path.join(destination, entry.entryName);
        if (entry.isDirectory) fs.mkdirSync(target, { recursive: true });
        else {
            fs.mkdirSync(path.dirname(target), { recursive: true });
            const data = entry.getData(); // adm-zip enforces declared size and bounded inflation.
            fs.writeFileSync(target, data, { mode: 0o600, flag: 'wx' });
        }
    }
    let root = destination;
    if (!fs.existsSync(path.join(root, 'manifest.json'))) {
        const candidates = fs.readdirSync(root, { withFileTypes: true }).filter(e => e.isDirectory() &&
            e.name !== '__MACOSX' && fs.existsSync(path.join(root, e.name, 'manifest.json')));
        if (candidates.length !== 1) throw new Error('Package must contain manifest.json at its root or inside one wrapper folder.');
        root = path.join(root, candidates[0].name);
    }
    const manifest = validateManifest(root);
    if (crx) {
        manifest.key = crx.key;
        fs.writeFileSync(path.join(root, 'manifest.json'), JSON.stringify(manifest, null, 2));
    }
    return { root, manifest, id: crx?.id };
}

function copyUnpacked(source, destination) {
    if (path.resolve(destination).startsWith(path.resolve(source) + path.sep)) {
        throw new Error('Choose the extension folder containing manifest.json, not the profile or extensions directory.');
    }
    let total = 0, count = 0;
    const copy = (from, to) => {
        const stat = fs.lstatSync(from);
        if (++count > MAX_FILES || stat.isSymbolicLink()) throw new Error('Unpacked extension has too many files or symbolic links.');
        if (stat.isDirectory()) {
            fs.mkdirSync(to, { recursive: true });
            for (const name of fs.readdirSync(from)) copy(path.join(from, name), path.join(to, name));
        } else if (stat.isFile()) {
            total += stat.size;
            if (total > MAX_EXPANDED) throw new Error('Extension exceeds 256 MB.');
            fs.copyFileSync(from, to, fs.constants.COPYFILE_EXCL);
            fs.chmodSync(to, 0o600);
        } else throw new Error('Extension contains a special file.');
    };
    copy(source, destination);
    return { root: destination, manifest: validateManifest(destination) };
}

async function downloadStorePackage(id, chromeVersion, fetcher = fetch) {
    const url = new URL('https://clients2.google.com/service/update2/crx');
    url.search = new URLSearchParams({ response: 'redirect', prodversion: chromeVersion,
        acceptformat: 'crx2,crx3', x: `id=${storeId(id)}&uc` }).toString();
    const signal = AbortSignal.timeout(60000);
    let response, target = url;
    for (let redirects = 0; redirects <= 5; redirects++) {
        response = await fetcher(target, { signal, redirect: 'manual' });
        if (![301, 302, 303, 307, 308].includes(response.status)) break;
        const next = new URL(response.headers.get('location'), target);
        await response.body?.cancel();
        if (next.protocol !== 'https:') throw new Error('Insecure extension download redirect.');
        target = next;
    }
    if (!response.ok || !response.body) throw new Error(`Chrome Web Store download failed (${response.status}).`);
    const chunks = []; let size = 0;
    for await (const chunk of response.body) {
        size += chunk.length;
        if (size > MAX_PACKAGE) throw new Error('Extension package exceeds 64 MB.');
        chunks.push(Buffer.from(chunk));
    }
    return Buffer.concat(chunks);
}

module.exports = { storeId, extensionId, parseCrx, validateManifest, unpackPackage, copyUnpacked, downloadStorePackage, MAX_PACKAGE };
