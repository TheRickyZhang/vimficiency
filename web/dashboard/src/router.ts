import { createRootRoute, createRoute, createRouter, redirect, Outlet } from '@tanstack/react-router';
import type { BenchmarkData, BenchmarkRun } from './types/benchmark';
import type { ExplorationData } from './types/exploration';
import type { CoverageData, CoverageBinary } from './types/coverage';
import { loadBenchmarkData, discoverCategories, type TimePoint } from './utils/data';
import { exploreCategoryNames } from './utils/exploration';
import { RootLayout } from './components/RootLayout';
import { HomePage } from './pages/HomePage';
import { OptimizerPage } from './pages/OptimizerPage';
import { TestSuitePage } from './pages/TestSuitePage';
import { ExplorePage } from './pages/ExplorePage';
import { ChunkDetailPage } from './pages/ChunkDetailPage';
import {
  isTestSuiteSlug,
  loadTestSuiteBenchmarkData,
  binaryTotalSeries,
  getTestSuite,
  type TestSuiteKind,
} from './utils/testSuites';

const VALID_OPTIMIZERS = ['edit', 'motion', 'composition', 'tests'] as const;
export type OptimizerSlug = (typeof VALID_OPTIMIZERS)[number];

const base = import.meta.env.BASE_URL; // '/vimficiency/'

export const rootRoute = createRootRoute({
  component: RootLayout,
});

async function fetchJson<T>(path: string): Promise<T | null> {
  try {
    const response = await fetch(`${base}${path}?_=${Date.now()}`);
    return response.ok ? await response.json() : null;
  } catch {
    return null;
  }
}

function benchmarkSearch(search: Record<string, unknown>) {
  return { cat: (search.cat as string) || undefined, bench: (search.bench as string) || undefined };
}

export const homeRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: '/',
  loader: async () => {
    const [results, coverage] = await Promise.all([
      Promise.all(VALID_OPTIMIZERS.map(async slug => {
        const data = await fetchJson<BenchmarkData>(`${slug}/data.json`);
        return data && { slug, data };
      })),
      fetchJson<CoverageData>('tests/coverage.json'),
    ]);
    return { optimizers: results.filter(r => r !== null), coverage };
  },
  component: HomePage,
});

const optimizerLayoutRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: '$optimizer',
  beforeLoad: ({ params }) => {
    if (!(VALID_OPTIMIZERS as readonly string[]).includes(params.optimizer)) {
      throw redirect({ to: '/' });
    }
  },
  component: Outlet,
});

export interface OptimizerLoaderData {
  data: ReturnType<typeof loadBenchmarkData> | null;
  categories: ReturnType<typeof discoverCategories>;
  optimizerName: string;
  repoUrl: string;
  // Categories that actually have exploration data, for gating Explore buttons.
  // null = unknown (explore.json missing/failed): don't gate, preserve old behavior.
  exploreCategories: string[] | null;
}

function emptyOptimizerData(): OptimizerLoaderData {
  return { data: null, categories: {}, optimizerName: 'Benchmarks', repoUrl: '', exploreCategories: null };
}

async function fetchExplorationData({ params }: { params: { optimizer: string } }) {
  const data = await fetchJson<ExplorationData>(`${params.optimizer}/explore.json`);
  if (data && !Array.isArray(data.entries)) {
    console.warn(`explore.json for "${params.optimizer}" is malformed (missing entries[]); ignoring`);
    return null;
  }
  return data;
}

export const optimizerIndexRoute = createRoute({
  getParentRoute: () => optimizerLayoutRoute,
  path: '/',
  loader: async ({ params }): Promise<OptimizerLoaderData> => {
    const isTests = params.optimizer === 'tests';
    try {
      const [raw, exploration] = await Promise.all([
        fetchJson<BenchmarkData>(`${params.optimizer}/data.json`),
        isTests ? null : fetchExplorationData({ params }),
      ]);
      if (!raw) return emptyOptimizerData();
      const exploreCategories = exploration && exploreCategoryNames(exploration);
      const data = isTests
        ? loadTestSuiteBenchmarkData(raw, 'unit')
        : loadBenchmarkData(raw);
      const categories = discoverCategories(data);
      let optimizerName = 'Benchmarks';
      const firstName = data[data.length - 1]?.benches[0]?.name;
      if (isTests) {
        optimizerName = 'Unit Tests';
      } else if (firstName) {
        optimizerName = firstName.split('/')[0] ?? 'Benchmarks';
      }
      return { data: data.length > 0 ? data : null, categories, optimizerName, repoUrl: raw.repoUrl, exploreCategories };
    } catch {
      return emptyOptimizerData();
    }
  },
  validateSearch: benchmarkSearch,
  component: OptimizerPage,
});

export interface TestSuiteLoaderData {
  kind: TestSuiteKind;
  optimizerName: string;
  repoUrl: string;
  data: BenchmarkRun[] | null;
  categories: Record<string, string[]>;
  binaryTotal: TimePoint[];
  coverage: CoverageBinary | null;
}

function emptyTestSuiteData(kind: TestSuiteKind, optimizerName: string): TestSuiteLoaderData {
  return { kind, optimizerName, repoUrl: '', data: null, categories: {}, binaryTotal: [], coverage: null };
}

export const testSuiteRoute = createRoute({
  getParentRoute: () => optimizerLayoutRoute,
  path: '$testSuite',
  beforeLoad: ({ params }) => {
    if (params.optimizer !== 'tests' || !isTestSuiteSlug(params.testSuite)) {
      throw redirect({ to: '/' });
    }
  },
  loader: async ({ params }): Promise<TestSuiteLoaderData> => {
    const slug = params.testSuite;
    if (!isTestSuiteSlug(slug)) return emptyTestSuiteData('timing', 'Tests');

    const { kind, title, label } = getTestSuite(slug);
    const empty = emptyTestSuiteData(kind, title);
    try {
      if (kind === 'coverage') {
        const snapshot = await fetchJson<CoverageData>('tests/coverage.json');
        return { ...empty, coverage: snapshot?.binaries[label] ?? null };
      }

      const raw = await fetchJson<BenchmarkData>('tests/data.json');
      if (!raw) return empty;
      const data = loadTestSuiteBenchmarkData(raw, slug);
      return {
        ...empty,
        repoUrl: raw.repoUrl,
        data: data.length > 0 ? data : null,
        categories: discoverCategories(data),
        binaryTotal: binaryTotalSeries(raw, slug, raw.repoUrl),
      };
    } catch {
      return empty;
    }
  },
  validateSearch: benchmarkSearch,
  component: TestSuitePage,
});

const exploreOptions = {
  getParentRoute: () => optimizerLayoutRoute,
  beforeLoad: ({ params }: { params: { optimizer: string } }) => {
    if (params.optimizer === 'tests') {
      throw redirect({ to: '/$optimizer', params: { optimizer: 'tests' }, search: { cat: undefined, bench: undefined } });
    }
  },
  loader: fetchExplorationData,
};

export const exploreRoute = createRoute({
  ...exploreOptions,
  path: '/explore',
  validateSearch: (search: Record<string, unknown>) => ({
    case: (search['case'] as string) || undefined,
  }),
  component: ExplorePage,
});

export const chunkDetailRoute = createRoute({
  ...exploreOptions,
  path: '/explore/chunk',
  validateSearch: (search: Record<string, unknown>) => ({
    case: (search['case'] as string) || undefined,
    chunkIndex: search['chunkIndex'] != null ? Number(search['chunkIndex']) : undefined,
  }),
  component: ChunkDetailPage,
});

const routeTree = rootRoute.addChildren([
  homeRoute,
  optimizerLayoutRoute.addChildren([optimizerIndexRoute, chunkDetailRoute, exploreRoute, testSuiteRoute]),
]);

export const router = createRouter({
  routeTree,
  basepath: base.replace(/\/$/, ''), // '/vimficiency'
  defaultPreload: 'intent',
});

declare module '@tanstack/react-router' {
  interface Register {
    router: typeof router;
  }
}
