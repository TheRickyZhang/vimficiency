def cell:
  if . == null then null
  elif (.step == "MOVE" or .step == "CHANGE" or .step == "CROSS") then {cost,step,pi,pj}
  else {cost,step} end;

def grid($i0; $i1; $j0; $j1):
  .cells | with_entries(.value |= (.[0][$i0:$i1] | map(.[$j0:$j1] | map(cell))));

def small:
  .plans[0] as $plan | .optPath[0].cost as $penalty |
  {example, grid: grid(0; (.example.initial | length) + 1; 0; (.example.goal | length) + 1), path: [.optPath[] | {i,j,table,step}],
   plan: {cost: $plan.cost, delete: $plan.regions[0].del, insert: $plan.regions[0].ins},
   penalty: $penalty, entry, esc, demoDelete: .delCost[0][2], demoMove: .moveCost[2][4],
   split: {delete: (.delCost[0][2] + .delCost[5][7]), move: .moveCost[2][5],
           type: ((.typed[0:2] + .typed[5:7]) | add)},
   focus: {
     exit: .cells.in[0][5][5].cost,
     move: .cells.out[0][2][2].cost, moveSpan: .moveCost[2][5],
     delete: .cells.out[0][0][5].cost, deleteSpan: .delCost[0][5],
     winner: .cells.out[0][5][5].step
   }};

def large:
  {i0:24, i1:34, j0:26, j1:42} as $win |
  {example, blocks, plan: {cost: .plans[0].cost, regions: [.plans[0].regions[] | {aBegin,aEnd,bBegin,bEnd}]},
   insetWin: $win, grid: grid($win.i0; $win.i1 + 1; $win.j0; $win.j1 + 1)};
