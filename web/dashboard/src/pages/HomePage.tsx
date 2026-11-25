import { Fragment, type ReactNode } from 'react';
import { Link } from '@tanstack/react-router';
import { homeRoute, type OptimizerSlug } from '../router';
import { benchmarkChanges, toNanoseconds, loadBenchmarkData, discoverCategories } from '../utils/data';
import { fmtTime, ns, type Nanoseconds } from '../utils/format';
import { TEST_SUITES, summarizeTestSuite, binaryTotalName } from '../utils/testSuites';
import { ChangesSection } from '../components/ChangesSection';
import { MultiLineChart, type ChartLine } from '../components/MultiLineChart';
import type { BenchmarkData } from '../types/benchmark';

const CARD_CLASS = 'block card card-hover p-5 no-underline text-inherit';

const OPTIMIZER_LABELS: Partial<Record<OptimizerSlug, string>> = {
  edit: 'TransformOptimizer',
  motion: 'NavOptimizer',
  composition: 'CompositionOptimizer',
};

const TEST_LINES: ChartLine[] = [
  { label: 'Unit', benchName: binaryTotalName('unit'), color: '#4285f4' },
  { label: 'Approval', benchName: binaryTotalName('approval'), color: '#9c27b0' },
];

// Summed wall-time of every bench in the latest commit, with the % change from
// the previous commit. Mirrors summarizeTestSuite for the optimizer cards.
function summarizeOptimizer(data: BenchmarkData): { total: Nanoseconds; trend: number | null; categories: number } | null {
  const runs = loadBenchmarkData(data);
  const latest = runs[runs.length - 1];
  if (!latest) return null;
  const prev = runs.length > 1 ? runs[runs.length - 2] : undefined;
  let lSum = 0, pSum = 0;
  for (const b of latest.benches) lSum += toNanoseconds(b.value, b.unit);
  if (prev) for (const b of prev.benches) pSum += toNanoseconds(b.value, b.unit);
  const trend = (prev && pSum > 0) ? (lSum - pSum) / pSum * 100 : null;
  return { total: ns(lSum), trend, categories: Object.keys(discoverCategories(runs)).length };
}

export function HomePage() {
  const { optimizers, coverage } = homeRoute.useLoaderData();

  const changes = optimizers.flatMap(({ slug, data }) => slug === 'tests' ? [] :
    benchmarkChanges(loadBenchmarkData(data)).map(c => ({ ...c, optimizer: slug })));

  const testOptimizer = optimizers.find((o) => o.slug === 'tests');
  const testRuns = testOptimizer ? loadBenchmarkData(testOptimizer.data) : [];
  const hasTests = testRuns.length > 0 || coverage !== null;

  return (
    <>
      <h1>Vimficiency Benchmarks</h1>
      <p className="text-[#666] text-[1.1rem] mb-10">Performance tracking across commits</p>

      <h2>Optimizer Benchmarks</h2>
      <div className="grid grid-cols-[repeat(auto-fit,minmax(280px,1fr))] gap-4">
        {(['edit', 'motion', 'composition'] as const).map((slug) => {
          const optData = optimizers.find((o) => o.slug === slug)?.data;
          const sum = optData ? summarizeOptimizer(optData) : null;
          const segments: ReactNode[] = sum
            ? [
              `${sum.categories} categor${sum.categories !== 1 ? 'ies' : 'y'}`,
              sum.trend !== null ? <Trend pct={sum.trend} /> : null,
              fmtTime(sum.total),
            ]
            : ['No data'];
          return (
            <HomeCard
              key={slug}
              nav={{ kind: 'optimizer', slug }}
              title={OPTIMIZER_LABELS[slug] ?? slug}
              segments={segments}
            />
          );
        })}
      </div>

      {hasTests && (
        <div className="mt-10">
          <h2>Tests</h2>
          <div className="grid grid-cols-[repeat(auto-fit,minmax(220px,1fr))] gap-4">
            {TEST_SUITES.map((suite) => {
              if (suite.kind === 'timing') {
                const s = testOptimizer ? summarizeTestSuite(testOptimizer.data, suite.slug) : null;
                const segments: ReactNode[] = s
                  ? [
                    s.suiteCount > 0 ? `${s.suiteCount} suite${s.suiteCount !== 1 ? 's' : ''}` : null,
                    s.trend !== null ? <Trend pct={s.trend} /> : null,
                    s.latestDuration != null ? fmtTime(s.latestDuration) : null,
                  ]
                  : ['No data'];
                return (
                  <HomeCard key={suite.slug} nav={{ kind: 'suite', slug: suite.slug }} title={suite.title} segments={segments} />
                );
              }
              const binary = coverage?.binaries[suite.label] ?? null;
              const segments: ReactNode[] = binary
                ? [
                  `${binary.suites.length} suite${binary.suites.length !== 1 ? 's' : ''}`,
                  `${binary.totalCases} case${binary.totalCases !== 1 ? 's' : ''}`,
                  <Passing failures={binary.failures} />,
                ]
                : ['No data'];
              return (
                <HomeCard key={suite.slug} nav={{ kind: 'suite', slug: suite.slug }} title={suite.title} segments={segments} />
              );
            })}
          </div>
        </div>
      )}

      {testRuns.length > 0 && (
        <div className="mt-10 max-w-[60%] mx-auto max-md:max-w-full">
          <h2>C++ Test Duration (Unit + Approval)</h2>
          <div style={{ height: 250 }}>
            <MultiLineChart runs={testRuns} lines={TEST_LINES} />
          </div>
        </div>
      )}

      <div className="mt-10">
        <ChangesSection changes={changes} limit={5} renderItem={(c, body) => (
          <Link key={`${c.optimizer}-${c.name}`} to="/$optimizer" params={{ optimizer: c.optimizer }}
            search={{ cat: c.category, bench: c.name }} className="change-item no-underline">{body}</Link>
        )} />
      </div>
    </>
  );
}

type CardNav =
  | { kind: 'optimizer'; slug: OptimizerSlug }
  | { kind: 'suite'; slug: string };

function HomeCard({ nav, title, segments }: { nav: CardNav; title: string; segments: ReactNode[] }) {
  const body = (
    <>
      <div className="font-bold text-[1.4rem] text-[#333] mb-1">{title}</div>
      <MetaLine segments={segments} />
    </>
  );
  return nav.kind === 'optimizer' ? (
    <Link to="/$optimizer" params={{ optimizer: nav.slug }} search={{ cat: undefined, bench: undefined }} className={CARD_CLASS}>
      {body}
    </Link>
  ) : (
    <Link to="/$optimizer/$testSuite" params={{ optimizer: 'tests', testSuite: nav.slug }} search={{ cat: undefined, bench: undefined }} className={CARD_CLASS}>
      {body}
    </Link>
  );
}

function MetaLine({ segments }: { segments: ReactNode[] }) {
  const kept = segments.filter((s) => s !== null && s !== undefined && s !== '');
  return (
    <div className="text-[0.9rem] text-muted">
      {kept.map((s, i) => <Fragment key={i}>{i > 0 && ' · '}{s}</Fragment>)}
    </div>
  );
}

function Trend({ pct }: { pct: number }) {
  const color = pct > 5 ? 'text-bad' : pct < -5 ? 'text-good' : 'text-muted';
  return <span className={`font-semibold ${color}`}>{pct > 0 ? '+' : ''}{pct.toFixed(1)}%</span>;
}

function Passing({ failures }: { failures: number }) {
  return (
    <span className={`font-semibold ${failures === 0 ? 'text-good' : 'text-bad'}`}>
      {failures === 0 ? 'all passing' : `${failures} failing`}
    </span>
  );
}
