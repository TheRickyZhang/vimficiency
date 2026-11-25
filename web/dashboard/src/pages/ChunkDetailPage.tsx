import { Link } from '@tanstack/react-router';
import { useMemo } from 'react';
import { chunkDetailRoute } from '../router';
import { BufferPreview } from '../components/BufferPreview';
import { ExplorationResults } from '../components/ExplorationResults';
import { chunkDisplayKey, deriveChunkDetail, findCase, isChunkedCase } from '../utils/exploration';

export function ChunkDetailPage() {
  const data = chunkDetailRoute.useLoaderData();
  const { case: caseName, chunkIndex } = chunkDetailRoute.useSearch();
  const { optimizer } = chunkDetailRoute.useParams();
  const latest = data?.entries[data.entries.length - 1];
  const activeCase = latest && caseName ? findCase(latest.cases, caseName) : undefined;
  const detail = useMemo(() => activeCase && isChunkedCase(activeCase) && chunkIndex != null
    ? deriveChunkDetail(activeCase, chunkIndex)
    : null, [activeCase, chunkIndex]);

  if (!detail || !caseName || !activeCase) {
    let message = 'No detail data available for this chunk.';
    if (!latest) message = 'No exploration data available.';
    else if (!caseName || chunkIndex == null) message = 'Missing case or chunkIndex parameter.';
    else if (!activeCase || !isChunkedCase(activeCase)) message = 'Case not found or not a chunked case.';
    return <><h1>Chunk Detail</h1><p className="subtitle">{message}</p></>;
  }

  const type = detail.chunk?.type ?? 'motion';
  return (
    <>
      <div className="flex items-center gap-3 mb-2">
        <Link to="/$optimizer/explore" params={{ optimizer }} search={{ case: caseName }} className="link-brand text-sm">
          &larr; Back to explorer
        </Link>
      </div>
      <h1>
        <span className={type === 'edit' ? 'text-[#e65100]' : 'text-[#1565c0]'}>{type}</span>
        {' chunk: '}<code>{detail.chunk ? chunkDisplayKey(detail.chunk) : `chunk ${chunkIndex}`}</code>
      </h1>
      <p className="text-[#666] text-[1.1rem] mb-10">Detail view for chunk {chunkIndex} of case <code>{caseName}</code></p>
      {detail.context && <BufferPreview context={detail.context} diffs={type === 'edit' ? undefined : activeCase.diffs} />}
      <ExplorationResults
        key={`${latest?.date}:${activeCase.name}:${chunkIndex}`}
        states={detail.states}
        results={detail.results}
        treeLabel={`${detail.states.length.toLocaleString()} states`}
        emptyMessage="No exploration states for this chunk."
      />
    </>
  );
}
