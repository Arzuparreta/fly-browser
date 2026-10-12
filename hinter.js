(function() {
    if (window.hasHinter) return;
    window.hasHinter = true;

    // Preserved left-hand cluster: avoids standard hjkl and vim navigation keys
    const charset = ['q', 'w', 'e', 'a', 's', 'd', 'z', 'x', 'c', 'r', 'f', 'v'];

    function collectInteractiveElements(root) {
        const selector = 'a, button, input, textarea, select, details, summary, [role="button"], [role="link"], [role="tab"], [role="menuitem"], [role="checkbox"], [role="radio"], [role="switch"], [role="option"], [contenteditable="true"], [tabindex]:not([tabindex="-1"]), [onclick]';
        const elements = [];

        function traverse(node) {
            if (!node) return;
            if (node.nodeType === 1) {
                if (node.matches && node.matches(selector)) {
                    elements.push(node);
                }
                // Recursively penetrate open shadow roots in Web Components
                if (node.shadowRoot) {
                    traverse(node.shadowRoot);
                }
            }
            let child = node.firstElementChild;
            while (child) {
                traverse(child);
                child = child.nextElementSibling;
            }
        }

        traverse(root);
        return elements;
    }

    function generateHintCodes(count, alphabet) {
        const base = alphabet.length;
        let len = 1;
        let maxCombos = base;
        while (maxCombos < count) {
            len++;
            maxCombos = Math.pow(base, len);
        }

        const codes = [];
        for (let i = 0; i < count; i++) {
            let code = '';
            let num = i;
            for (let pos = len - 1; pos >= 0; pos--) {
                const digit = Math.floor(num / Math.pow(base, pos)) % base;
                code += alphabet[digit];
            }
            codes.push(code);
        }
        return { codes, codeLength: len };
    }

    window.toggleHints = function() {
        const existing = document.getElementById('mise-hint-layer');
        if (existing) { 
            if (typeof window.hintCleanup === 'function') {
                window.hintCleanup();
            } else {
                existing.remove(); 
                if (window.hintKeyListener) {
                    document.removeEventListener('keydown', window.hintKeyListener, true);
                }
            }
            return; 
        }

        const candidates = collectInteractiveElements(document);
        
        const visible = [];
        const seenRects = [];

        candidates.forEach(el => {
            const rect = el.getBoundingClientRect();
            if (rect.width <= 0 || rect.height <= 0) return;
            
            // Standard viewport intersection check
            if (rect.bottom <= 0 || rect.top >= window.innerHeight || rect.right <= 0 || rect.left >= window.innerWidth) {
                return;
            }

            try {
                const style = window.getComputedStyle(el);
                if (style.visibility === 'hidden' || style.display === 'none' || parseFloat(style.opacity) === 0) {
                    return;
                }
            } catch (err) {}

            // Deduplicate overlapping parent/child clickables at identical coordinates
            const isDuplicate = seenRects.some(r => 
                Math.abs(r.left - rect.left) < 5 && 
                Math.abs(r.top - rect.top) < 5 &&
                Math.abs(r.width - rect.width) < 5 &&
                Math.abs(r.height - rect.height) < 5
            );
            if (!isDuplicate) {
                seenRects.push(rect);
                visible.push({ el, rect });
            }
        });

        if (visible.length === 0) return;

        const layer = document.createElement('div');
        layer.id = 'mise-hint-layer';
        layer.style.cssText = 'position: fixed; top: 0; left: 0; width: 100vw; height: 100vh; pointer-events: none; z-index: 2147483647;';
        document.documentElement.appendChild(layer);

        const { codes, codeLength } = generateHintCodes(visible.length, charset);
        const activeHints = {};
        const badgeElements = [];
        let keysPressed = "";

        visible.forEach((item, index) => {
            const code = codes[index];
            activeHints[code] = item.el;

            const badge = document.createElement('div');
            badge.textContent = code;
            badge.dataset.code = code;
            badge.style.cssText = `
                position: fixed; left: ${Math.round(item.rect.left)}px; top: ${Math.round(item.rect.top)}px;
                background: #2ac3de; color: #1a1b26; padding: 2px 5px; font-size: 12px;
                line-height: 14px; font-weight: 800; border-radius: 3px; z-index: 2147483647;
                font-family: monospace; text-transform: uppercase; border: 1px solid #1a1b26;
                box-shadow: 0 2px 4px rgba(0,0,0,0.4); pointer-events: none; transition: opacity 0.1s ease;
            `;
            layer.appendChild(badge);
            badgeElements.push(badge);
        });

        function cleanup() {
            layer.remove();
            document.removeEventListener('keydown', window.hintKeyListener, true);
            window.removeEventListener('resize', cleanup, true);
            window.removeEventListener('scroll', cleanup, true);
            document.removeEventListener('fullscreenchange', cleanup, true);
            window.hintCleanup = null;
        }

        window.hintCleanup = cleanup;
        window.addEventListener('resize', cleanup, { once: true, capture: true });
        window.addEventListener('scroll', cleanup, { once: true, capture: true });
        document.addEventListener('fullscreenchange', cleanup, { once: true, capture: true });

        function updateBadgeHighlighting() {
            badgeElements.forEach(b => {
                const bCode = b.dataset.code;
                if (!keysPressed || bCode.startsWith(keysPressed)) {
                    b.style.opacity = '1';
                    b.style.background = keysPressed ? '#ff9e64' : '#2ac3de';
                } else {
                    b.style.opacity = '0.2';
                    b.style.background = '#2ac3de';
                }
            });
        }

        window.hintKeyListener = function(e) {
            if (e.key === 'Escape') {
                e.preventDefault();
                e.stopPropagation();
                e.stopImmediatePropagation();
                cleanup();
                return;
            }

            if (e.key === 'Backspace') {
                e.preventDefault();
                e.stopPropagation();
                e.stopImmediatePropagation();
                if (keysPressed.length > 0) {
                    keysPressed = keysPressed.slice(0, -1);
                    updateBadgeHighlighting();
                }
                return;
            }

            const key = e.key.toLowerCase();
            if (charset.includes(key)) {
                e.preventDefault();
                e.stopPropagation();
                e.stopImmediatePropagation();
                keysPressed += key;

                updateBadgeHighlighting();

                if (activeHints[keysPressed]) {
                    const target = activeHints[keysPressed];
                    cleanup();

                    if (typeof target.click === 'function') {
                        target.click();
                    } else {
                        target.dispatchEvent(new MouseEvent('click', { bubbles: true, cancelable: true, view: window }));
                    }
                    if (typeof target.focus === 'function') {
                        target.focus();
                    }
                } else if (keysPressed.length >= codeLength) {
                    keysPressed = "";
                    updateBadgeHighlighting();
                }
            } else {
                // Trap any other non-hint keystroke so it does not trigger underlying page or Vim navigation
                e.preventDefault();
                e.stopPropagation();
                e.stopImmediatePropagation();
            }
        };

        document.addEventListener('keydown', window.hintKeyListener, true);
    };
})();
