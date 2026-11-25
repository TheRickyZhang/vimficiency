import type { ReactNode } from 'react';
import type { BenchmarkChange } from '../utils/data';

type Change = BenchmarkChange & { optimizer?: string };

interface Props<T extends Change> {
  changes: T[];
  limit: number;
  renderItem: (change: T, body: ReactNode) => ReactNode;
}

export function ChangesSection<T extends Change>({ changes, limit, renderItem }: Props<T>) {
  function column(title: string, items: T[]) {
    return (
      <div>
        <h3>{title}</h3>
        {items.length === 0 ? <p className="text-sm text-muted">None</p> : (
          <div className="flex flex-col gap-1">
            {items.slice(0, limit).map(c => renderItem(c, <>
              <div className="flex flex-col gap-px min-w-0">
                <span className="font-semibold text-[0.9rem] truncate">{c.detail}</span>
                <span className="text-xs text-[#999]">
                  {c.optimizer && <span className="capitalize">{c.optimizer} / </span>}{c.category}
                </span>
              </div>
              <div className="flex items-center gap-2 shrink-0">
                {Math.abs(c.pct) >= 50 && <span className="text-[1.1rem] text-warn" title="Exceeds alert threshold">⚠</span>}
                {!c.optimizer && <span className="text-[0.8rem] text-muted">{c.ratio.toFixed(2)}&times;</span>}
                <span className={`font-bold text-[0.9rem] tabular-nums ${c.pct > 0 ? 'text-bad' : 'text-good'}`}>
                  {c.pct > 0 && '+'}{c.pct.toFixed(1)}%
                </span>
              </div>
            </>))}
          </div>
        )}
      </div>
    );
  }

  return (
    <div className="grid grid-cols-2 gap-6 mb-8 max-md:grid-cols-1">
      {column('Largest Regressions', changes.filter(c => c.pct > 0).sort((a, b) => b.pct - a.pct))}
      {column('Largest Improvements', changes.filter(c => c.pct < 0).sort((a, b) => a.pct - b.pct))}
    </div>
  );
}
