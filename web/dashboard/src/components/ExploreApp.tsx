import { useState, useMemo, useEffect, useRef } from 'react';
import type { ExplorationData } from '../types/exploration';
import { BufferPreview } from './BufferPreview';
import { ExplorationResults } from './ExplorationResults';
import { caseCategoryExists, findCase, flattenChunkEntry, isChunkedCase, parseCaseName } from '../utils/exploration';

interface Props {
  data: ExplorationData;
  initialCase: string | null;
}

export function ExploreApp({ data, initialCase }: Props) {
  const [commitIdx, setCommitIdx] = useState<number>(data.entries.length - 1);

  const entry = data.entries[commitIdx]!;
  const cases = entry.cases;

  // Resolve initial case, defaulting to first case
  const resolvedInitial = useMemo(() => findCase(cases, initialCase), [cases, initialCase]);
  const [selectedCaseName, setSelectedCaseName] = useState<string | null>(null);

  // Active case: use explicit selection if set, otherwise resolved initial
  const activeCase = selectedCaseName
    ? cases.find((c) => c.name === selectedCaseName) ?? resolvedInitial
    : resolvedInitial;

  const isLatestCommit = commitIdx === data.entries.length - 1;

  // Parse cases into categories and params
  const { categories, paramsByCategory } = useMemo(() => {
    const cats = new Map<string, string[]>();
    for (const c of cases) {
      const { category, param } = parseCaseName(c.name);
      if (!cats.has(category)) cats.set(category, []);
      cats.get(category)!.push(param);
    }
    return { categories: [...cats.keys()], paramsByCategory: cats };
  }, [cases]);

  const activeCategory = activeCase ? parseCaseName(activeCase.name).category : categories[0] ?? '';
  const activeParam = activeCase ? parseCaseName(activeCase.name).param : '';
  const paramsForCategory = paramsByCategory.get(activeCategory) ?? [];

  const setCategory = (cat: string) => {
    const params = paramsByCategory.get(cat);
    if (params && params.length > 0) setSelectedCaseName(`${cat}/${params[0]}`);
  };

  const setParam = (param: string) => {
    setSelectedCaseName(`${activeCategory}/${param}`);
  };

  const traces = useMemo(() => {
    if (!activeCase) return null;
    return isChunkedCase(activeCase)
      ? { states: activeCase.states.map(flattenChunkEntry), results: activeCase.results.map(flattenChunkEntry), composition: activeCase }
      : { states: activeCase.states, results: activeCase.results ?? [] };
  }, [activeCase]);

  // Commit step
  const stepCommit = (delta: number) => {
    setCommitIdx((prev) => {
      const next = prev + delta;
      if (next < 0 || next >= data.entries.length) return prev;
      return next;
    });
    setSelectedCaseName(null);
  };

  return (
    <div>
      <h1>Search Space Explorer</h1>
      <p className="text-[#666] text-[1.1rem] mb-10">A* explored states visualization</p>

      {initialCase && !selectedCaseName && !caseCategoryExists(cases, initialCase) && (
        <div className="mb-6 rounded-md border border-[#e6c200] bg-[#fffbe6] text-[#7a5d00] text-sm px-4 py-3 text-center">
          No exploration data for <code className="font-semibold">{initialCase}</code>
          {activeCase && <> — showing <code className="font-semibold">{activeCase.name}</code> instead.</>}
          {categories.length > 0 && <> Explored: {categories.join(', ')}.</>}
        </div>
      )}

      {/* Commit row */}
      <div className="mb-4 flex flex-col items-center">
        <label className="form-label">Commit</label>
        <div className="flex items-center gap-1">
          <button
            onClick={() => stepCommit(-1)}
            disabled={commitIdx <= 0}
            className="px-2 py-1.5 rounded-md border border-[#ccc] bg-surface text-sm cursor-pointer disabled:opacity-30 disabled:cursor-default hover:bg-[#f0f0f0]"
            title="Previous commit"
          >&larr;</button>
          <SearchableSelect
            value={String(commitIdx)}
            options={data.entries.map((e, i) => ({
              value: String(i),
              label: `${e.commit.id.substring(0, 7)} — ${e.commit.message.split('\n')[0]?.substring(0, 40)}`,
            }))}
            onChange={(v) => { setCommitIdx(Number(v)); setSelectedCaseName(null); }}
          />
          <button
            onClick={() => stepCommit(1)}
            disabled={commitIdx >= data.entries.length - 1}
            className="px-2 py-1.5 rounded-md border border-[#ccc] bg-surface text-sm cursor-pointer disabled:opacity-30 disabled:cursor-default hover:bg-[#f0f0f0]"
            title="Next commit"
          >&rarr;</button>
        </div>
      </div>

      {/* Type / Size row */}
      <div className="flex gap-4 mb-6 flex-wrap items-end justify-center">
        <div>
          <label className="form-label">Type</label>
          <SearchableSelect
            value={activeCategory}
            options={categories.map((cat) => ({ value: cat, label: cat }))}
            onChange={setCategory}
          />
        </div>

        {paramsForCategory.length > 0 && (
          <div>
            <label className="form-label">Size</label>
            <SearchableSelect
              value={activeParam}
              options={paramsForCategory.map((p) => {
                const c = cases.find((c) => c.name === `${activeCategory}/${p}`);
                const suffix = c
                  ? c.states.length > 0
                    ? ` (${c.states.length} st)`
                    : ` (${c.nodesExplored} nodes)`
                  : '';
                return { value: p, label: `${p}${suffix}` };
              })}
              onChange={setParam}
            />
          </div>
        )}
      </div>

      {activeCase?.context && (
        <BufferPreview context={activeCase.context} diffs={activeCase.diffs} />
      )}

      {activeCase && traces && (
        <ExplorationResults
          key={`${commitIdx}:${entry.date}:${activeCase.name}`}
          {...traces}
          treeLabel={`${activeCase.nodesExplored.toLocaleString()} nodes explored`}
          emptyMessage={<>
            {activeCase.nodesExplored === 0
              ? 'This case was not run for this commit.'
              : isLatestCommit
                ? 'Exploration tree and charts are unavailable: this build did not record per-state traces. Check that VIMF_TRACK_STATES was set when the explore binary was built.'
                : 'Exploration tree and charts are only available for the latest commit (older commits are stored in summary-only form).'}
            <span className="block text-xs mt-1">{activeCase.nodesExplored.toLocaleString()} nodes were explored.</span>
          </>}
        />
      )}
    </div>
  );
}

// Searchable dropdown (filterable options)
function SearchableSelect({ value, options, onChange }: {
  value: string;
  options: { value: string; label: string }[];
  onChange: (value: string) => void;
}) {
  const [open, setOpen] = useState(false);
  const [filter, setFilter] = useState('');
  const ref = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  // Close on outside click
  useEffect(() => {
    if (!open) return;
    const handler = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) setOpen(false);
    };
    document.addEventListener('mousedown', handler);
    return () => document.removeEventListener('mousedown', handler);
  }, [open]);

  // Focus input when opened
  useEffect(() => {
    if (open) {
      setFilter('');
      setTimeout(() => inputRef.current?.focus(), 0);
    }
  }, [open]);

  const current = options.find((o) => o.value === value);
  const filtered = filter
    ? options.filter((o) => o.label.toLowerCase().includes(filter.toLowerCase()))
    : options;

  return (
    <div ref={ref} className="relative">
      <button
        onClick={() => setOpen(!open)}
        className="px-3 py-1.5 text-[0.9rem] rounded-md border border-[#ccc] bg-surface text-left min-w-[120px] cursor-pointer hover:bg-[#f8f8f8] flex items-center gap-1"
      >
        <span className="flex-1 truncate">{current?.label ?? value}</span>
        <span className="text-[0.7rem] text-muted">{open ? '\u25B2' : '\u25BC'}</span>
      </button>
      {open && (
        <div className="absolute z-20 top-full mt-1 left-0 min-w-full bg-white border border-[#ccc] rounded-md shadow-lg max-h-[300px] flex flex-col">
          {options.length > 5 && (
            <input
              ref={inputRef}
              value={filter}
              onChange={(e) => setFilter(e.target.value)}
              placeholder="Search..."
              className="px-2 py-1.5 text-sm border-b border-[#eee] outline-none"
            />
          )}
          <div className="overflow-y-auto">
            {filtered.map((o) => (
              <div
                key={o.value}
                onClick={() => { onChange(o.value); setOpen(false); }}
                className={`px-3 py-1.5 text-[0.9rem] cursor-pointer hover:bg-[#f0f4ff] ${
                  o.value === value ? 'bg-[#e3f2fd] font-semibold' : ''
                }`}
              >
                {o.label}
              </div>
            ))}
            {filtered.length === 0 && (
              <div className="px-3 py-2 text-sm text-muted">No matches</div>
            )}
          </div>
        </div>
      )}
    </div>
  );
}
