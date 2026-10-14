// tests/notes-reader-export.test.js
// Automated verification for notes expansion, Reader View export, and Alt-modifier exclusion

const assert = require('assert');
const keybinds = require('../keybinds.js');
const { parseMarkdownToHtml, formatArticleToMarkdown } = require('../modules/overlays/notes.js');

console.log('Running Fly notes overlay and Reader View export regression tests...');

// 1. Verify export-reader-to-notes keybinding is registered
{
    const defaults = keybinds.DEFAULT_KEYBINDS;
    assert(defaults['export-reader-to-notes'], 'export-reader-to-notes must be present in default keybinds');

    const bindings = Array.isArray(defaults['export-reader-to-notes'])
        ? defaults['export-reader-to-notes']
        : [defaults['export-reader-to-notes']];

    assert(bindings.includes('Ctrl+Shift+E'), 'export-reader-to-notes must include Ctrl+Shift+E');
    console.log('✓ Export reader to notes default keybinds registered');
}

// 2. The new export shortcut must not conflict with the fork's browser bindings.
{
    const keyboard = require('../keyboard.js');
    const result = keyboard.validateConfig(keybinds.getDefaults(), keybinds.getActionMetadata());
    assert(result.ok, JSON.stringify(result.errors));
    console.log('✓ Export shortcut coexists with personalised browser bindings');
}

// 3. Verify action metadata
{
    const metadata = keybinds.getActionMetadata();
    assert(metadata['export-reader-to-notes'], 'export-reader-to-notes metadata must exist');
    assert(metadata['export-reader-to-external'], 'export-reader-to-external metadata must exist');
    assert(metadata['open-notes-external'], 'open-notes-external metadata must exist');
    assert.strictEqual(metadata['export-reader-to-notes'].category, 'Web Interaction');
    assert.strictEqual(metadata['export-reader-to-external'].category, 'Web Interaction');
    assert.strictEqual(metadata['open-notes-external'].category, 'Web Interaction');

    console.log('✓ Notes and reader export action metadata verified');
}

// 4. Verify input event translation to key combination
{
    const eventE = {
        type: 'keyDown',
        key: 'e',
        code: 'KeyE',
        control: true,
        shift: true,
        alt: false,
        meta: false
    };
    const comboE = keybinds.inputEventToKeyCombination(eventE);
    assert.strictEqual(comboE, 'Ctrl+Shift+E', 'Ctrl+Shift+E input event must resolve to Ctrl+Shift+E');

    const actionE = keybinds.getActionForInput(eventE);
    assert.strictEqual(actionE, 'export-reader-to-notes', 'Ctrl+Shift+E must resolve to export-reader-to-notes action');

    console.log('✓ Key combination normalisation for export shortcut verified');
}

// 5. Verify markdown parsing with headers, links, and rules
{
    const source = '# Header 1\n## Header 2\n[Fly Browser](https://flybrowser.example)\n***\n> Quoted excerpt';
    const html = parseMarkdownToHtml(source);

    assert(html.includes('<h1>Header 1</h1>'), 'Must render h1 tag');
    assert(html.includes('<h2>Header 2</h2>'), 'Must render h2 tag');
    assert(html.includes('<a href="https://flybrowser.example" target="_blank" rel="noopener noreferrer">Fly Browser</a>'), 'Must render anchor link');
    assert(html.includes('<hr>'), 'Must render horizontal rule');
    assert(html.includes('<blockquote>Quoted excerpt</blockquote>'), 'Must render blockquote');

    console.log('✓ Markdown parser formatting and link resolution verified');
}

// 6. Verify article distillation to markdown formatting
{
    const sampleArticle = {
        title: 'Exploring Web Performance',
        url: 'https://example.com/article',
        byline: 'Jane Doe',
        siteName: 'example.com',
        excerpt: 'An overview of modern rendering optimisations.',
        textContent: 'Paragraph one of the article.\n\nParagraph two with more details.'
    };

    const markdown = formatArticleToMarkdown(sampleArticle);
    assert(markdown.includes('## [Exploring Web Performance](https://example.com/article)'), 'Must format header with link');
    assert(markdown.includes('*Author:* Jane Doe'), 'Must include author byline');
    assert(markdown.includes('*Source:* example.com'), 'Must include source domain');
    assert(markdown.includes('> An overview of modern rendering optimisations.'), 'Must include quote excerpt');
    assert(markdown.includes('Paragraph one of the article.'), 'Must include article body paragraphs');

    console.log('✓ Article distillation and markdown formatting verified');
}

// 7. Verify keyboard navigation and Escape dismissal logic
{
    const { state } = require('../modules/state.js');
    const { handleNotesGlobalKeyDown } = require('../modules/overlays/notes.js');

    // Test Escape dismissal
    state.notesActive = true;
    state.isEditingNotes = false;

    let defaultPrevented = false;
    let propagationStopped = false;
    const fakeEscapeEvent = {
        key: 'Escape',
        preventDefault: () => { defaultPrevented = true; },
        stopPropagation: () => { propagationStopped = true; },
        stopImmediatePropagation: () => {}
    };

    handleNotesGlobalKeyDown(fakeEscapeEvent);
    assert.strictEqual(defaultPrevented, true, 'Escape must call preventDefault');
    assert.strictEqual(propagationStopped, true, 'Escape must stop propagation');
    assert.strictEqual(state.notesActive, false, 'Escape must toggle notesActive to false');

    // Test 'e' key switches to edit mode in preview
    state.notesActive = true;
    state.isEditingNotes = false;
    const fakeEEvent = {
        key: 'e',
        preventDefault: () => {},
        stopPropagation: () => {},
        stopImmediatePropagation: () => {}
    };
    handleNotesGlobalKeyDown(fakeEEvent);
    assert.strictEqual(state.isEditingNotes, true, 'Pressing e in preview mode must switch to edit mode');

    // Test 'Ctrl+E' switches back to preview mode in edit mode
    const fakeCtrlEEvent = {
        key: 'e',
        ctrlKey: true,
        preventDefault: () => {},
        stopPropagation: () => {},
        stopImmediatePropagation: () => {}
    };
    handleNotesGlobalKeyDown(fakeCtrlEEvent);
    assert.strictEqual(state.isEditingNotes, false, 'Pressing Ctrl+E in edit mode must switch back to preview mode');

    // Clean up state
    state.notesActive = false;

    console.log('✓ Notes overlay keyboard navigation and Escape dismissal verified');
}

// 8. Verify selected text formatting to markdown for context menu clipping
{
    const { formatSelectionToMarkdown, parseMarkdownToHtml } = require('../modules/overlays/notes.js');

    const sampleSelection = {
        text: 'Fly is an efficient, privacy-focused, keyboard-centric web browser.\nBuilt with Electron on Linux.',
        title: 'Fly Browser Project',
        url: 'https://flybrowser.example'
    };

    const markdown = formatSelectionToMarkdown(sampleSelection);
    assert(markdown.includes('### [Fly Browser Project](https://flybrowser.example)'), 'Must include title with link');
    assert(markdown.includes('*Captured:'), 'Must include capture timestamp');
    assert(markdown.includes('> Fly is an efficient, privacy-focused, keyboard-centric web browser.'), 'Must prefix first line with blockquote');
    assert(markdown.includes('> Built with Electron on Linux.'), 'Must prefix second line with blockquote');

    const html = parseMarkdownToHtml(markdown);
    assert(html.includes('<h3><a href="https://flybrowser.example" target="_blank" rel="noopener noreferrer">Fly Browser Project</a></h3>'), 'Must render linked h3 heading');
    assert(html.includes('<blockquote>Fly is an efficient, privacy-focused, keyboard-centric web browser.<br>Built with Electron on Linux.</blockquote>'), 'Must render cohesive merged blockquote');

    // Test fallback when URL is missing
    const noUrlSelection = {
        text: 'Local document clipping',
        title: 'Document.pdf'
    };
    const noUrlMarkdown = formatSelectionToMarkdown(noUrlSelection);
    assert(noUrlMarkdown.includes('### Document.pdf\n'), 'Must render plain title without link when url missing');

    console.log('✓ Context menu selection clipping formatting and markdown parsing verified');
}

console.log('\nAll notes overlay and Reader View export tests passed successfully!');
