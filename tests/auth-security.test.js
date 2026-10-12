// tests/auth-security.test.js
// Automated pre-flight regression tests for Google authentication and network security

const assert = require('assert');
const path = require('path');

// Mock minimal electron requirements if running in standalone Node
const Module = require('module');
const origRequire = Module.prototype.require;
Module.prototype.require = function(id) {
    if (id === 'electron') {
        return {
            app: {
                getPath: () => path.join(__dirname, '..')
            },
            ipcMain: {
                handle: () => {},
                on: () => {}
            }
        };
    }
    return origRequire.apply(this, arguments);
};

const security = require('../security.js');

console.log('Running Mise authentication & network security tests...');

// 1. Test User-Agent sanitisation for Google sign-in
{
    const sampleDefaultUa = 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Mise/0.14.0 Chrome/132.0.6834.196 Electron/34.0.0 Safari/537.36';
    security.setGoogleSignInUserAgent(sampleDefaultUa);

    const uaForAccounts = security.getGoogleSignInUserAgent('accounts.google.com');
    assert(uaForAccounts.includes('Electron/34.0.0'), 'Must retain Electron version token');
    assert(uaForAccounts.includes('Chrome/132.0.6834.196'), 'Must retain Chrome version token');
    assert(!uaForAccounts.includes('Mise/'), 'Must strip application name token');
    assert(uaForAccounts.startsWith('Mozilla/5.0'), 'Must maintain valid browser prefix');

    // Ensure non-accounts domains do not receive the Electron override
    assert.strictEqual(security.getGoogleSignInUserAgent('google.com'), '', 'google.com must not receive sign-in override');
    assert.strictEqual(security.getGoogleSignInUserAgent('mail.google.com'), '', 'mail.google.com must not receive sign-in override');
    assert.strictEqual(security.getGoogleSignInUserAgent('github.com'), '', 'github.com must not receive sign-in override');

    console.log('✓ User-Agent sanitisation and host scoping verified');
}

// 2. Test Google sign-in request classification (isGoogleSignInRequest)
{
    // Main frame navigations to accounts.google.com
    const mainNavAccounts = {
        resourceType: 'mainFrame',
        url: 'https://accounts.google.com/ServiceLogin?service=mail'
    };
    assert.strictEqual(security.isGoogleSignInRequest(mainNavAccounts), true, 'Main frame accounts navigation must be identified');

    // Main frame navigations to regular search
    const mainNavGoogle = {
        resourceType: 'mainFrame',
        url: 'https://www.google.com/search?q=test'
    };
    assert.strictEqual(security.isGoogleSignInRequest(mainNavGoogle), false, 'Regular Google navigation must not be identified');

    // Subresource requests originating from an accounts.google.com page
    const subresourceOnAccounts = {
        resourceType: 'xhr',
        url: 'https://apis.google.com/js/api.js',
        webContents: {
            getURL: () => 'https://accounts.google.com/v3/signin/identifier'
        }
    };
    assert.strictEqual(security.isGoogleSignInRequest(subresourceOnAccounts), true, 'Subresource on accounts page must be identified');

    // Subresource requests originating from another domain
    const subresourceOnOther = {
        resourceType: 'xhr',
        url: 'https://apis.google.com/js/api.js',
        webContents: {
            getURL: () => 'https://mail.google.com/mail/u/0/'
        }
    };
    assert.strictEqual(security.isGoogleSignInRequest(subresourceOnOther), false, 'Subresource on other page must not be identified');

    console.log('✓ Google sign-in request classification verified');
}

// 3. Test onBeforeRequest WebLiteSignIn redirection and listener preservation
{
    let beforeRequestListener = null;
    let headersReceivedListener = null;

    const mockSession = {
        webRequest: {
            onBeforeRequest: (filter, listener) => { beforeRequestListener = listener; },
            onHeadersReceived: (filter, listener) => { headersReceivedListener = listener; },
            onBeforeSendHeaders: () => {}
        }
    };

    security.attachNetworkHandlers(mockSession);
    assert(typeof beforeRequestListener === 'function', 'attachNetworkHandlers must register onBeforeRequest');
    assert(typeof headersReceivedListener === 'function', 'attachNetworkHandlers must register onHeadersReceived');

    // Test GlifWebSignIn -> WebLiteSignIn URL rewrite
    let redirectResult = null;
    beforeRequestListener(
        { url: 'https://accounts.google.com/v3/signin/identifier?flowName=GlifWebSignIn&dsh=S123' },
        (res) => { redirectResult = res; }
    );
    assert(redirectResult && redirectResult.redirectURL, 'GlifWebSignIn must trigger redirectURL');
    assert(redirectResult.redirectURL.includes('flowName=WebLiteSignIn'), 'Target must be rewritten to WebLiteSignIn');
    assert(!redirectResult.redirectURL.includes('flowName=GlifWebSignIn'), 'GlifWebSignIn token must be removed');

    // Test that already-correct WebLiteSignIn passes through without re-redirecting
    let passThroughResult = null;
    beforeRequestListener(
        { url: 'https://accounts.google.com/v3/signin/identifier?flowName=WebLiteSignIn' },
        (res) => { passThroughResult = res; }
    );
    assert(passThroughResult && !passThroughResult.redirectURL, 'WebLiteSignIn must pass through without redirect');

    // Test that spoofed parameter on non-Google host is ignored
    let nonGoogleResult = null;
    beforeRequestListener(
        { url: 'https://example.com/login?flowName=GlifWebSignIn' },
        (res) => { nonGoogleResult = res; }
    );
    assert(nonGoogleResult && !nonGoogleResult.redirectURL, 'Non-Google host must not be rewritten');

    console.log('✓ onBeforeRequest Google sign-in URL rewriting verified');
}

// 4. Test PDF viewer redirection in onHeadersReceived
{
    let headersListener = null;
    const mockSession = {
        webRequest: {
            onBeforeRequest: () => {},
            onHeadersReceived: (filter, listener) => { headersListener = listener; },
            onBeforeSendHeaders: () => {}
        }
    };

    security.attachNetworkHandlers(mockSession);

    // Main frame PDF request
    let pdfRedirectResult = null;
    headersListener(
        {
            resourceType: 'mainFrame',
            url: 'https://example.com/document.pdf',
            responseHeaders: { 'Content-Type': ['application/pdf'] }
        },
        (res) => { pdfRedirectResult = res; }
    );
    assert(pdfRedirectResult && pdfRedirectResult.redirectURL, 'PDF document must trigger redirect');
    assert(pdfRedirectResult.redirectURL.includes('viewer.html'), 'PDF must redirect to internal viewer');

    // Non-PDF HTML request
    let htmlResult = null;
    headersListener(
        {
            resourceType: 'mainFrame',
            url: 'https://example.com/index.html',
            responseHeaders: { 'Content-Type': ['text/html'] }
        },
        (res) => { htmlResult = res; }
    );
    assert(htmlResult && !htmlResult.redirectURL, 'HTML document must pass through without redirect');

    console.log('✓ onHeadersReceived PDF viewer redirection verified');
}

console.log('\nAll authentication & network security tests passed successfully!\n');
