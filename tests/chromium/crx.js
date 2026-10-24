// Reads a signed Chrome extension package (CRX2 or CRX3) for the live media
// test: verifies its signature and identity, then unpacks it with the Web
// Store key added to manifest.json so the extension keeps its store ID.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');

function extensionId(key) {
    return [...crypto.createHash('sha256').update(key).digest('hex').slice(0, 32)]
        .map(c => String.fromCharCode(97 + parseInt(c, 16))).join('');
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

// Extracts the package's zip with Python's zipfile, refusing absolute paths,
// `..` and symbolic links.
const EXTRACT = `
import os, stat, sys, zipfile
archive, target = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(archive) as z:
    for info in z.infolist():
        parts = info.filename.split('/')
        if info.filename.startswith('/') or '\\\\' in info.filename or '..' in parts:
            sys.exit('unsafe path in extension package: ' + info.filename)
        if stat.S_ISLNK(info.external_attr >> 16):
            sys.exit('symbolic link in extension package: ' + info.filename)
    z.extractall(target)
`;

function unpackPackage(buffer, destination, expectedId) {
    const crx = parseCrx(buffer, expectedId);
    fs.mkdirSync(destination, {recursive: true, mode: 0o700});
    const archive = destination + '.zip';
    fs.writeFileSync(archive, crx.contents, {mode: 0o600});
    try {
        const result = spawnSync('python3', ['-I', '-c', EXTRACT, archive, destination], {encoding: 'utf8'});
        if (result.status !== 0) throw new Error(result.stderr.trim() || 'Could not unpack the extension package.');
    } finally {
        fs.rmSync(archive, {force: true});
    }
    const file = path.join(destination, 'manifest.json');
    const manifest = JSON.parse(fs.readFileSync(file, 'utf8'));
    manifest.key = crx.key;
    fs.writeFileSync(file, JSON.stringify(manifest, null, 2));
    return {root: destination, manifest, id: crx.id};
}

module.exports = {extensionId, parseCrx, unpackPackage};
