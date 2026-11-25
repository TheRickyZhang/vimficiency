import type {
  ExplorationData,
  ExplorationCase,
  SequenceChunk,
  ChunkedCase,
  ChunkedResultEntry,
} from '../types/exploration';

export const SUPERSCRIPTS = ['\u00B9', '\u00B2', '\u00B3', '\u2074', '\u2075', '\u2076', '\u2077', '\u2078', '\u2079'];

export function isChunkedCase(c: ExplorationCase): c is ChunkedCase {
  return Array.isArray(c.contents);
}

export function chunkDisplayKey(c: SequenceChunk): string {
  if (c.contentId != null && c.contentId >= 0) return c.text + (SUPERSCRIPTS[c.contentId] ?? `[${c.contentId}]`);
  return c.text;
}

export function flattenChunkEntry({ effort, chunks }: ChunkedResultEntry) {
  return { effort, chunks, tokens: chunks.map(chunkDisplayKey) };
}

export function findCase(cases: ExplorationCase[], query: string | null): ExplorationCase | undefined {
  if (!query) return cases[0];
  const exact = cases.find((c) => c.name === query);
  if (exact) return exact;
  // Exact param missing: land on the same category's first case if it exists,
  // otherwise the first case overall (caseCategoryExists tells the caller it was a miss).
  const queryCat = parseCaseName(query).category;
  const sameCat = cases.find((c) => parseCaseName(c.name).category === queryCat);
  return sameCat ?? cases[0];
}

export function parseCaseName(name: string): { category: string; param: string } {
  const slash = name.indexOf('/');
  if (slash === -1) return { category: name, param: '' };
  return { category: name.substring(0, slash), param: name.substring(slash + 1) };
}

/** Categories present in the latest exploration entry — used to gate Explore buttons. */
export function exploreCategoryNames(data: ExplorationData): string[] {
  const latest = data.entries[data.entries.length - 1];
  if (!latest) return [];
  const cats = new Set<string>();
  for (const c of latest.cases) cats.add(parseCaseName(c.name).category);
  return [...cats];
}

/** Whether any case shares the requested query's category. */
export function caseCategoryExists(cases: ExplorationCase[], query: string | null): boolean {
  if (!query) return true;
  const cat = parseCaseName(query).category;
  return cases.some((c) => parseCaseName(c.name).category === cat);
}

export function expandableChunks(c: ChunkedCase) {
  const indices = new Set<number>();
  let editIndex = 0;
  for (const [index, chunk] of (c.results[0]?.chunks ?? []).entries()) {
    if (chunk.type !== 'edit') {
      indices.add(index);
    } else {
      if (c.diffs?.[editIndex]?.editStates?.length) indices.add(index);
      editIndex++;
    }
  }
  return indices;
}

export function deriveChunkDetail(c: ChunkedCase, index: number) {
  const chunks = c.results[0]?.chunks ?? [];
  if (!Number.isInteger(index) || index < 0) return null;
  const chunk = chunks[index];
  const editsBefore = chunks.slice(0, index).filter(ch => ch.type === 'edit').length;
  if (chunk?.type === 'edit') {
    const diff = c.diffs?.[editsBefore];
    if (!diff) return null;
    return {
      chunk,
      states: diff.editStates ?? [],
      results: diff.transformResults ?? diff.editResults ?? [],
      context: { initialLines: (diff.deletedText ?? '').split('\n'), goalLines: (diff.insertedText ?? '').split('\n') },
    };
  }

  function motionAt(entry: ChunkedResultEntry) {
    const motion = entry.chunks[index];
    return motion && motion.type !== 'edit' ? [{ effort: entry.effort, tokens: motion.tokens }] : [];
  }

  const states = c.states.flatMap(s => s.editsCompleted === editsBefore ? motionAt(s) : []);
  const results = c.results.flatMap(motionAt);
  if (!chunk && !states.length && !results.length) return null;
  return { chunk, states, results, context: c.context };
}
