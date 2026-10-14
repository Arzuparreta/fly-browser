# The Google Auth Botguard Saga: The rrk=46 Riddle and Poisoned Cookies

How signing into Gmail inside an isolated container turned into a chase through Botguard detection, a User-Agent trap, and cookies that kept re-triggering their own rejection.

> *Update, October 2026: A contributor, Arzuparreta, tested this fix and found that part of it never ran. Electron keeps only one listener for each web request event, and the ad blocker's listener replaced the one that rewrote the sign-in flow. The real cause was the User-Agent. With the standardised Chrome User-Agent, Google runs its full Chrome checks, which an embedded app fails. Mise now sends Electron's own User-Agent, without the app name, to accounts.google.com only, and sign-in works. The code below is the first version, kept for the record. The fix is in pull request #19.*

---

## 1. The Problem

Mise's workspaces run as separate, isolated Electron partitions, so Work, Personal, and Mail never share cookies or session state. Opening Gmail or Google Workspace inside one of those partitions hit an immediate wall:

> **Couldn't sign you in**
> This browser or app may not be secure. Learn more.

The URL carried a rejection token: `rrk=46`. No password field ever appeared, and refreshing or re-entering credentials changed nothing.

---

## 2. The First Attempt

The usual fix for Google rejecting an embedded Chromium runtime is User-Agent filtering, on the theory that Google is flagging the `<webview>`'s runtime tokens. So the first attempt was:

1. Override `app.userAgentFallback` with a plain desktop Chrome User-Agent string.
2. Strip Electron-specific tokens from outgoing headers.
3. Keep the usual privacy shields running: hardware concurrency, canvas, and fingerprint farbling in `webview-preload.js`.

The idea was simple: look like an ordinary desktop Chrome session.

---

## 3. Why That Made It Worse

### Trap 1: GlifWebSignIn and Botguard

A synthetic or altered User-Agent routes the request into `flowName=GlifWebSignIn`, Google's full single-sign-on client. That flow runs **Botguard**, Google's anti-bot check, which validates the runtime at a platform level. An embedded `<webview>` is missing native Chrome platform components that Botguard checks for, so it gets flagged as automated and served `rrk=46` immediately. The fingerprint farbling on `navigator` added further heuristic flags on top of that.

```
Synthetic User-Agent -> routed to GlifWebSignIn -> Botguard runs -> platform check fails -> rrk=46
```

### Trap 2: The Rejection Writes Its Own Cookies

The confusing part was that reverting the User-Agent change didn't fix anything either. Once `rrk=46` fires once, Google writes `__Host-GAPS` and `OTZ` cookies into that partition. Every later request to `accounts.google.com` sends those cookies back, and Google short-circuits straight to the rejection page before any login logic even runs. The partition was stuck rejecting itself.

---

## 4. The Actual Fix

Three separate changes, each closing one part of the loop.

### A. Rewrite the Flow Before It Leaves the Browser

Google also serves a lighter authentication flow built for embedded and lower-friction logins: `flowName=WebLiteSignIn`. Rather than fight Botguard, Mise intercepts the request and rewrites it before it's sent:

```javascript
// Route Google authentication requests through the WebLite sign-in flow
if (targetSession.webRequest && typeof targetSession.webRequest.onBeforeRequest === 'function') {
    targetSession.webRequest.onBeforeRequest({ urls: ['*://accounts.google.com/*'] }, (details, callback) => {
        const url = details.url || '';
        if (url.includes('flowName=GlifWebSignIn')) {
            const redirectedUrl = url.replaceAll('flowName=GlifWebSignIn', 'flowName=WebLiteSignIn');
            return callback({ redirectURL: redirectedUrl });
        }
        callback({});
    });
}
```

### B. Leave Auth Domains Alone

User-Agent spoofing and fingerprint farbling are both switched off entirely for Google's authentication endpoints, which are added to the existing trusted-domain allowlist:

```javascript
// Keep unmodified headers on trusted authentication domains
if (isTrustedDomain(hostname)) {
    return callback({ requestHeaders: details.requestHeaders });
}
```

With the domain trusted, `webview-preload.js` skips its farbling pass entirely there, so Google's sign-in engine sees a genuine, internally consistent browser profile.

### C. Clean Up After a Rejection, Automatically

To stop a single rejection from locking a partition out permanently, any `rrk=46` rejection now triggers an automatic cookie purge:

```javascript
// In main.js: detect rejection navigation and trigger cleanup
webContents.on('did-navigate', (navEvent, url) => {
    logVisit(webContents.getTitle(), url);
    if (url && url.includes('accounts.google.com') && url.includes('signin/rejected')) {
        security.clearGoogleAuthCookies(webContents.session);
    }
});
```

```javascript
// In security.js: strip the poisoned tokens
async function clearGoogleAuthCookies(targetSession) {
    if (!targetSession || !targetSession.cookies) return;
    try {
        const domains = ['google.com', 'accounts.google.com', 'mail.google.com'];
        for (const dom of domains) {
            const cookies = await targetSession.cookies.get({ domain: dom });
            for (const c of cookies) {
                if (c.name.startsWith('__Host-') || c.name === 'OTZ' || c.name === 'NID' || c.name.startsWith('__Secure-')) {
                    const scheme = c.secure ? 'https://' : 'http://';
                    const cookieUrl = scheme + c.domain.replace(/^\./, '') + c.path;
                    await targetSession.cookies.remove(cookieUrl, c.name);
                }
            }
        }
    } catch (e) {}
}
```

---

## What This Actually Showed

Spoofing harder wasn't the answer here, in either direction. Blocking fingerprint data outright makes a browser look like a bot. Faking a generic desktop identity on an endpoint that specifically checks platform authenticity does the same thing, just via a different signal. The fix wasn't more disguise, it was knowing which domains to leave completely alone, and cleaning up the one piece of state (the rejection cookies) that kept the failure alive after the original cause was already gone.
