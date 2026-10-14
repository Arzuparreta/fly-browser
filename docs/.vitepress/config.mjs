import { defineConfig } from 'vitepress';

export default defineConfig({
  title: 'Mise Browser',
  description: 'Keyboard-first, container-isolated web browser for fanless Linux systems',
  base: '/Mise-browser/',
  head: [
    ['link', { rel: 'icon', href: '/Mise-browser/logo.png' }],
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:site_name', content: 'Mise Browser' }],
    ['meta', { property: 'og:title', content: 'Mise Browser' }],
    ['meta', { property: 'og:description', content: 'An efficient, keyboard-first, and container-isolated web browser engineered specifically for low-resource and fanless Linux hardware.' }],
    ['meta', { property: 'og:image', content: 'https://arzuparreta.github.io/Mise-browser/og-image.png' }],
    ['meta', { property: 'og:image:width', content: '1280' }],
    ['meta', { property: 'og:image:height', content: '640' }],
    ['meta', { property: 'og:url', content: 'https://arzuparreta.github.io/Mise-browser/' }],
    ['meta', { name: 'twitter:card', content: 'summary_large_image' }],
    ['meta', { name: 'twitter:title', content: 'Mise Browser' }],
    ['meta', { name: 'twitter:description', content: 'An efficient, keyboard-first, and container-isolated web browser engineered specifically for low-resource and fanless Linux hardware.' }],
    ['meta', { name: 'twitter:image', content: 'https://arzuparreta.github.io/Mise-browser/og-image.png' }]
  ],
  themeConfig: {
    logo: '/logo.png',
    siteTitle: 'Mise Browser',
    nav: [
      { text: 'Guide', link: '/guide/getting-started' },
      { text: 'Features', link: '/guide/navigation' },
      { text: 'Scripts', link: '/guide/community-scripts' },
      { text: 'Blog', link: '/blog/' },
      { text: 'Policies', link: '/guide/privacy-policy' },
      { text: 'Reference', link: '/reference/keybinds' },
      { text: 'GitHub', link: 'https://github.com/Arzuparreta/Mise-browser' }
    ],
    sidebar: [
      {
        text: 'Engineering Blog',
        items: [
          { text: 'Chronicles Overview', link: '/blog/' },
          { text: 'The Awakening Cascade', link: '/blog/tab-traversal-awakening-cascade' },
          { text: 'Dark Reader Dilemma', link: '/blog/dark-reader-navigation-persistence' },
          { text: 'Link Hints & Shadow DOM', link: '/blog/shadow-dom-link-hints' },
          { text: 'Accidental Tab Resurrection', link: '/blog/accidental-tab-resurrection' },
          { text: 'Google Auth & Botguard', link: '/blog/google-auth-rrk46-botguard' },
          { text: 'Shadow DOM Keyboard Trap', link: '/blog/shadow-dom-keyboard-trap' },
          { text: 'The "Zero-RAM" Tab Myth', link: '/blog/tab-hibernation-zero-ram-myth' }
        ]
      },
      {
        text: 'Getting Started',
        items: [
          { text: 'Overview & Philosophy', link: '/guide/getting-started' },
          { text: 'Installation & Setup', link: '/guide/installation' }
        ]
      },
      {
        text: 'Core Features',
        items: [
          { text: 'Mouse-Free Navigation', link: '/guide/navigation' },
          { text: 'Workspaces & Containers', link: '/guide/workspaces-and-containers' },
          { text: 'True Tab Hibernation', link: '/guide/tab-hibernation' },
          { text: 'Privacy & Trusted Sites', link: '/guide/privacy-and-shields' },
          { text: 'Notes, Bookmarks & Quickmarks', link: '/guide/notes-and-bookmarks' },
          { text: 'Terminal Security & Oversight', link: '/guide/terminal-oversight' },
          { text: 'User Scripts & Styles', link: '/guide/user-scripts-and-styles' },
          { text: 'Community Scripts', link: '/guide/community-scripts' },
          { text: 'Chrome Extensions (Opt-in)', link: '/guide/extensions' }
        ]
      },
      {
        text: 'Policies & Security',
        items: [
          { text: 'Privacy Policy', link: '/guide/privacy-policy' },
          { text: 'Security Policy', link: '/guide/security-policy' },
        ]
      },
      {
        text: 'Reference',
        items: [
          { text: 'Keyboard Shortcuts', link: '/reference/keybinds' },
          { text: 'Command Palette', link: '/reference/command-palette' },
          { text: 'Configuration Files', link: '/reference/configuration' },
          { text: 'Fork Performance Audit', link: '/performance-audit' },
          { text: 'Adoption and Review', link: '/adoption-review' }
        ]
      }
    ],
    search: {
      provider: 'local'
    },
    socialLinks: [
      { icon: 'github', link: 'https://github.com/Arzuparreta/Mise-browser' }
    ],
    footer: {
      message: 'Released under the GNU General Public Licence v3.0. • Independent continuation of <a href="https://github.com/Rakosn1cek/Mise-browser" target="_blank">Mise by Rakosn1cek</a>',
      copyright: 'Copyright © 2026 Mise Browser Project'
    }
  }
});
