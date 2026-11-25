import type { BenchmarkCommit } from './benchmark';

export interface ExploredStateEntry {
  effort: number;
  tokens: string[];
}

export interface FoundResultEntry {
  tokens: string[];
  effort: number;
}

// Chunked format (composition optimizer)
export interface SequenceChunk {
  type: 'movement' | 'motion' | 'edit';
  text: string;
  tokens: string[];
  contentId?: number;
}

export interface ChunkedStateEntry {
  effort: number;
  editsCompleted: number;
  chunks: SequenceChunk[];
}

export interface ChunkedResultEntry {
  effort: number;
  chunks: SequenceChunk[];
}

// Character-level diff region (composition optimizer)
export interface DiffRegion {
  beginLine: number;
  beginCol: number;
  endLine: number;
  endCol: number;
  deletedText: string;
  insertedText: string;
  // Per-diff edit exploration data (when trackExploredStates=true)
  editStates?: ExploredStateEntry[];
  transformResults?: FoundResultEntry[];
  editResults?: FoundResultEntry[];
}

export interface BufferContext {
  initialLines: string[];
  goalLines: string[];
  initialCursor?: [number, number];
  goalCursor?: [number, number];
  // Motion-to-range goal: half-open [beginLine, beginCol, endLine, endCol) span
  // of acceptable landing cells. Present instead of goalCursor for range cases.
  goalRange?: [number, number, number, number];
  prefix?: string;
  suffix?: string;
  hasLinesAbove?: boolean;
  hasLinesBelow?: boolean;
}

interface CaseContext {
  name: string;
  nodesExplored: number;
  context?: BufferContext;
  diffs?: DiffRegion[];
}

export interface ChunkedCase extends CaseContext {
  contents: string[];
  states: ChunkedStateEntry[];
  results: ChunkedResultEntry[];
}

export type ExplorationCase = ChunkedCase | (CaseContext & {
  contents?: undefined;
  states: ExploredStateEntry[];
  results: FoundResultEntry[];
});

export interface ExplorationCommitEntry {
  commit: BenchmarkCommit;
  date: number;
  cases: ExplorationCase[];
}

export interface ExplorationData {
  lastUpdate: number;
  entries: ExplorationCommitEntry[];
}
