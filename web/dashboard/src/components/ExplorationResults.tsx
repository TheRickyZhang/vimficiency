import { Fragment, useMemo, useRef, useState, type ReactNode } from 'react';
import { useNavigate, useParams } from '@tanstack/react-router';
import type { ChunkedCase, ExploredStateEntry, FoundResultEntry, SequenceChunk } from '../types/exploration';
import { chunkDisplayKey, deriveChunkDetail, expandableChunks, SUPERSCRIPTS } from '../utils/exploration';
import { EffortHistogram } from './EffortHistogram';
import { ExplorationTimeline } from './ExplorationTimeline';
import { ExplorationTree } from './ExplorationTree';
import { FullscreenButton } from './FullscreenButton';

interface Props {
  states: ExploredStateEntry[];
  results: (FoundResultEntry & { chunks?: SequenceChunk[] })[];
  treeLabel: string;
  emptyMessage: ReactNode;
  composition?: ChunkedCase;
}

function Card({ title, fullscreen, children }: { title: string; fullscreen?: boolean; children: ReactNode }) {
  const ref = useRef<HTMLDivElement>(null);
  return (
    <div ref={ref} className={`card p-5 relative ${fullscreen ? 'exploration-chart' : 'mb-6'}`}>
      <h3 className="text-base font-bold mb-3 text-[#333]">{title}</h3>
      {children}
      {fullscreen && <FullscreenButton target={ref} />}
    </div>
  );
}

function shortText(text: string) {
  return text.length > 30 ? text.slice(0, 27) + '...' : text;
}

export function ExplorationResults({ states, results, treeLabel, emptyMessage, composition }: Props) {
  const sorted = useMemo(() => [...results].sort((a, b) => a.effort - b.effort), [results]);
  const [selectedSeqs, setSelectedSeqs] = useState(() =>
    new Set<string>(sorted[0] ? [sorted[0].tokens.join('')] : []));
  const [selectedChunk, setSelectedChunk] = useState<{ index: number; text: string } | null>(null);
  const expandable = useMemo(() => composition && expandableChunks(composition), [composition]);
  const detail = useMemo(() => composition && selectedChunk && deriveChunkDetail(composition, selectedChunk.index),
    [composition, selectedChunk]);
  const navigate = useNavigate();
  const { optimizer } = useParams({ from: '/$optimizer' });

  function toggle(seq: string) {
    setSelectedSeqs(prev => {
      const next = new Set(prev);
      if (next.has(seq)) next.delete(seq);
      else next.add(seq);
      return next;
    });
  }

  return (
    <>
      {sorted.length > 0 && (
        <Card title="Found Sequences">
          <div className="flex flex-wrap gap-2 justify-center">
            {sorted.map(({ effort, tokens, chunks }, index) => {
              const seq = tokens.join('');
              const selected = selectedSeqs.has(seq);
              return (
                <button key={index} type="button" aria-pressed={selected} onClick={() => toggle(seq)}
                  className={`flex items-baseline gap-2 px-3.5 py-1.5 rounded-md cursor-pointer border-2 ${selected ? 'bg-[#e3f2fd] border-[#1976d2]' : 'bg-[#f5f5f5] border-border'}`}>
                  <code className="font-bold text-base" title={seq}>
                    {chunks ? chunks.map((chunk, i) => (
                      <Fragment key={i}>
                        {i > 0 && <span className="text-[#999]">·</span>}
                        <span className={chunk.type === 'edit' ? 'text-[#e65100]' : 'text-[#1565c0]'}>{chunkDisplayKey(chunk)}</span>
                      </Fragment>
                    )) : shortText(seq)}
                  </code>
                  <span className="text-[0.8rem] text-muted">{effort.toFixed(2)}</span>
                </button>
              );
            })}
          </div>
          {states.length > 0 && <p className="text-xs text-muted mt-1.5">Click to toggle paths in tree (multi-select)</p>}
          {!!composition?.contents.length && (
            <div className="flex flex-wrap gap-3 mt-3 pt-3 border-t border-[#eee]">
              {composition.contents.map((text, i) => (
                <div key={i} className="flex items-baseline gap-1.5 bg-[#fff8e1] px-3 py-1 rounded border border-[#ffe082]">
                  <span className="font-bold text-[#e65100]">{SUPERSCRIPTS[i] ?? `[${i}]`}</span>
                  <code className="text-sm text-[#333] max-w-[300px] truncate" title={text}>{shortText(text)}</code>
                </div>
              ))}
            </div>
          )}
        </Card>
      )}
      {states.length === 0 ? <p className="card p-5 mb-6 text-sm text-muted">{emptyMessage}</p> : (
        <>
          <Card title={`Exploration Tree (${treeLabel})`}>
            <div className="flex">
              <div className="flex-1 min-w-0">
                <ExplorationTree states={states} results={sorted} selectedSeqs={selectedSeqs}
                  expandableTokens={expandable}
                  onChunkDetail={(index, text) => setSelectedChunk(prev => prev?.index === index ? null : { index, text })}
                  onChunkDetailNewPage={composition && ((index) => navigate({
                    to: '/$optimizer/explore/chunk', params: { optimizer },
                    search: { case: composition.name, chunkIndex: index },
                  }))}
                />
              </div>
              {selectedChunk && detail && (
                <div className="flex-1 min-w-0 border-l border-[#e0e0e0]">
                  <div className="flex items-center justify-between mb-2 px-3 pt-1">
                    <h4 className="text-sm font-bold">
                      <span className={detail.chunk?.type === 'edit' ? 'text-[#e65100]' : 'text-[#1565c0]'}>{detail.chunk?.type ?? 'motion'}</span>
                      {' chunk: '}<code>{selectedChunk.text}</code>
                    </h4>
                    <button onClick={() => setSelectedChunk(null)} className="text-xs text-muted px-1">close</button>
                  </div>
                  {detail.states.length > 0
                    ? <ExplorationTree states={detail.states} results={detail.results} selectedSeqs={new Set()} />
                    : <p className="text-sm text-muted py-4">No detail states for this chunk</p>}
                </div>
              )}
            </div>
          </Card>
          <div className="grid grid-cols-2 gap-6 mb-6">
            <Card title="Effort Distribution" fullscreen>
              <EffortHistogram states={states} results={sorted} selectedSeq={selectedSeqs.values().next().value} />
            </Card>
            <Card title="Exploration Timeline" fullscreen><ExplorationTimeline states={states} /></Card>
          </div>
        </>
      )}
    </>
  );
}
