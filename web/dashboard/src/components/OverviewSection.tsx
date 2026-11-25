import type { BenchmarkRun } from '../types/benchmark';
import { benchmarkChanges } from '../utils/data';
import { SummaryCard } from './SummaryCard';
import { ChangesSection } from './ChangesSection';

interface Props {
  categories: Record<string, string[]>;
  data: BenchmarkRun[];
  onSelect: (category: string) => void;
  onSelectBench: (category: string, benchName: string) => void;
  categoryActionLabel?: string;
  onCategoryAction?: (category: string) => void;
}

export function OverviewSection({
  categories,
  data,
  onSelect,
  onSelectBench,
  categoryActionLabel,
  onCategoryAction,
}: Props) {
  const entries = Object.entries(categories);
  return (
    <div>
      <h2>Categories</h2>
      <div className="grid grid-cols-[repeat(auto-fit,minmax(200px,1fr))] gap-3 mb-8">
        {entries.map(([cat, names]) => (
          <SummaryCard
            key={cat}
            category={cat}
            benchNames={names}
            data={data}
            onClick={() => onSelect(cat)}
            actionLabel={categoryActionLabel}
            onAction={onCategoryAction ? () => onCategoryAction(cat) : undefined}
          />
        ))}
      </div>
      <ChangesSection changes={benchmarkChanges(data)} limit={3} renderItem={(c, body) => (
        <button key={c.name} className="change-item cursor-pointer text-left font-[inherit] w-full"
          onClick={() => onSelectBench(c.category, c.name)}>{body}</button>
      )} />
    </div>
  );
}
