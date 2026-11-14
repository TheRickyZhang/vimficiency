import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';
import { visit } from 'unist-util-visit';

const SITE_BASE = '/vimficiency/docs';

// Rewrites relative `*.md` hrefs in rendered HTML to Starlight slug routes
// (e.g. `01-installation.md` → `/vimficiency/docs/01-installation/`). The
// shipped `astro-rehype-relative-markdown-links` package can't see our
// custom `generateId` from content.config.ts (README → 'index'), and our
// content collection lives outside `src/content/` so its filesystem
// scanner misses the files. Inline keeps slug mapping logic in one place.
function rehypeRewriteMdLinks() {
  return (tree) => {
    visit(tree, 'element', (node) => {
      if (node.tagName !== 'a' || !node.properties) return;
      const href = node.properties.href;
      if (typeof href !== 'string') return;
      // Skip absolute, scheme-prefixed, and anchor-only links.
      if (/^([a-z]+:|\/\/|#|\/)/i.test(href)) return;
      const m = href.match(/^([^?#]*?)\.md(.*)?$/);
      if (!m) return;
      let pathPart = m[1].replace(/^\.\//, '');
      const suffix = m[2] || '';
      // README is the only file remapped via content.config.ts → 'index'.
      const slug = pathPart === 'README' ? '' : pathPart;
      const slugSegment = slug ? `${slug}/` : '';
      node.properties.href = `${SITE_BASE}/${slugSegment}${suffix}`;
    });
  };
}

// Starlight renders the frontmatter `title` as the page H1, so the body
// H1 that panvimdoc and GitHub need (see scripts/check-doc-titles.sh)
// would show twice. Drop it from the site only.
function remarkStripLeadingH1() {
  return (tree) => {
    const i = tree.children.findIndex((n) => n.type !== 'yaml' && n.type !== 'mdxjsEsm');
    if (i !== -1 && tree.children[i].type === 'heading' && tree.children[i].depth === 1) {
      tree.children.splice(i, 1);
    }
  };
}

// `base` matches the gh-pages mount point used by bench.yml: the
// dashboard ships at `/vimficiency/`, this site at `/vimficiency/docs/`.
export default defineConfig({
  site: 'https://therickyzhang.github.io',
  base: SITE_BASE,
  outDir: './dist',
  integrations: [
    starlight({
      title: 'Vimficiency',
      description: 'Watches how you edit Vim and surfaces shorter keystroke sequences.',
      social: {
        github: 'https://github.com/therickyzhang/vimficiency',
      },
      sidebar: [
        {
          label: 'Guide',
          // The glob loader records root-relative file paths
          // (`../../doc-src/NN-name.md`), and autogenerate matches on that
          // prefix — `'.'` matches nothing and leaves the group empty.
          autogenerate: { directory: '../../doc-src' },
        },
        {
          // Chapters opt out of autogenerate via `sidebar.hidden` so they
          // do not also appear nested under Guide.
          label: 'How it works',
          items: [
            { slug: 'how-it-works' },
            { slug: 'how-it-works/planning-a-change' },
            { slug: 'how-it-works/the-grid' },
            { slug: 'how-it-works/pricing-spans' },
            { slug: 'how-it-works/sealing' },
            { slug: 'how-it-works/searching' },
          ],
        },
      ],
      customCss: ['./src/styles/how-it-works.css'],
    }),
  ],
  // README.md and inter-chapter links use `.md`-relative form
  // (e.g. `[Installation](01-installation.md)`) so they keep working when
  // rendered on GitHub. The inline plugin above maps those hrefs to the
  // matching Starlight slug routes at build time.
  markdown: {
    remarkPlugins: [remarkStripLeadingH1],
    rehypePlugins: [rehypeRewriteMdLinks],
  },
});
