import { useEffect, useState, type RefObject } from 'react';

export function FullscreenButton({ target }: { target: RefObject<HTMLDivElement | null> }) {
  const [active, setActive] = useState(false);
  useEffect(() => {
    const changed = () => setActive(document.fullscreenElement === target.current);
    document.addEventListener('fullscreenchange', changed);
    return () => document.removeEventListener('fullscreenchange', changed);
  }, [target]);

  return (
    <button className="fullscreen-btn" title={active ? 'Exit fullscreen' : 'Fullscreen'}
      onClick={() => active ? document.exitFullscreen() : target.current?.requestFullscreen()}>
      <svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" strokeWidth="1.5">
        <path d={active ? 'M5 2v3H2M11 2v3h3M2 11h3v3M14 11h-3v3' : 'M2 6V2h4M10 2h4v4M14 10v4h-4M6 14H2v-4'} />
      </svg>
    </button>
  );
}
