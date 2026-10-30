import { defineConfig } from 'vitepress';

export default defineConfig({
  title: 'Fly Browser',
  description: 'Keyboard-first, container-isolated web browser for fanless Linux systems',
  base: '/fly-browser/',
  head: [
    ['link', { rel: 'stylesheet', href: '/fly-browser/fonts/fonts.css' }],
    ['link', { rel: 'icon', href: '/fly-browser/favicon.png' }],
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:site_name', content: 'Fly Browser' }],
    ['meta', { property: 'og:title', content: 'Fly Browser' }],
    ['meta', { property: 'og:description', content: 'An efficient, keyboard-first, and container-isolated web browser engineered specifically for low-resource and fanless Linux hardware.' }],
    ['meta', { property: 'og:image', content: 'https://arzuparreta.github.io/fly-browser/og-image.png' }],
    ['meta', { property: 'og:image:width', content: '1280' }],
    ['meta', { property: 'og:image:height', content: '640' }],
    ['meta', { property: 'og:url', content: 'https://arzuparreta.github.io/fly-browser/' }],
    ['meta', { name: 'twitter:card', content: 'summary_large_image' }],
    ['meta', { name: 'twitter:title', content: 'Fly Browser' }],
    ['meta', { name: 'twitter:description', content: 'An efficient, keyboard-first, and container-isolated web browser engineered specifically for low-resource and fanless Linux hardware.' }],
    ['meta', { name: 'twitter:image', content: 'https://arzuparreta.github.io/fly-browser/og-image.png' }]
  ],
  themeConfig: {
    logo: '/logo.png',
    siteTitle: 'Fly Browser',
    nav: [
      { text: 'Guide', link: '/guide/getting-started' },
      { text: 'Features', link: '/guide/navigation' },
      { text: 'Scripts', link: '/guide/community-scripts' },
      { text: 'Policies', link: '/guide/privacy-policy' },
      { text: 'Reference', link: '/reference/keybinds' },
      { text: 'GitHub', link: 'https://github.com/Arzuparreta/fly-browser' }
    ],
    sidebar: [
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
          { text: 'Tab Suspension', link: '/guide/tab-hibernation' },
          { text: 'Spell Checking', link: '/guide/spell-checking' },
          { text: 'Privacy & Ad Blocking', link: '/guide/privacy-and-shields' },
          { text: 'Bookmarks & History', link: '/guide/bookmarks-and-history' },
          { text: 'Terminal Security & Oversight', link: '/guide/terminal-oversight' },
          { text: 'User Scripts & Styles', link: '/guide/user-scripts-and-styles' },
          { text: 'Community Scripts', link: '/guide/community-scripts' },
          { text: 'Chrome Extensions', link: '/guide/extensions' }
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
          { text: 'Versioning & Releases', link: '/releasing' },
          { text: 'Chromium Development', link: '/chromium/build' },
          { text: 'Startup Performance Study', link: '/chromium/startup-study' },
          { text: 'Live Media Test', link: '/chromium/live-media' }
        ]
      }
    ],
    search: {
      provider: 'local'
    },
    socialLinks: [
      { icon: 'github', link: 'https://github.com/Arzuparreta/fly-browser' }
    ],
    footer: {
      message: 'Released under the GNU General Public Licence v3.0. • Independent continuation of <a href="https://github.com/Rakosn1cek/Mise-browser" target="_blank">Mise by Rakosn1cek</a>',
      copyright: 'Copyright © 2026 Fly Browser Project'
    }
  }
});
