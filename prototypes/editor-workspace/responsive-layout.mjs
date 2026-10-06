// Presentation only: the saved dock tree keeps its desktop placement and ratios.
const widths = {node:360, game:340, edit:340, lua:360, inspector:320, library:320, console:320};
export const minimumWidth = tree => !tree ? 0 : tree.type === 'leaf'
  ? widths[tree.view] ?? 280
  : tree.axis === 'x' ? minimumWidth(tree.a) + minimumWidth(tree.b) + 8 : Math.max(minimumWidth(tree.a), minimumWidth(tree.b));
export const stackLayout = (tree, width) => width < 650 || width < minimumWidth(tree);
export function fitDockTree(tree, width) {
  if (!tree || tree.type === 'leaf') return tree;
  if (tree.axis === 'y') return {...tree, a:fitDockTree(tree.a,width), b:fitDockTree(tree.b,width)};
  const space = width - 8;
  const ratio = Math.max(minimumWidth(tree.a)/space, Math.min(1-minimumWidth(tree.b)/space, tree.ratio));
  return {...tree, ratio, a:fitDockTree(tree.a,space*ratio), b:fitDockTree(tree.b,space*(1-ratio))};
}
export const minimumStackHeight = view => view === 'node' ? 360 : view === 'lua' ? 320 : 240;
export function preferredStackHeight(view, width, aspect=1.6) {
  if (view === 'node') return 620;
  if (view === 'lua' || view === 'library') return 440;
  if (view === 'inspector') return 560;
  if (view === 'game' || view === 'edit') return Math.max(280, Math.round((width-22)/aspect+104));
  return 340;
}
export function resizeStackPair(first, second, delta, firstMin=240, secondMin=240) {
  const total = first + second;
  const next = Math.max(firstMin, Math.min(total-secondMin, first+delta));
  return [next, total-next];
}

const views = tree => !tree ? [] : tree.type === 'leaf' ? [tree.view] : [...views(tree.a), ...views(tree.b)];
const dockHeights = {node:520, lua:320, inspector:360, library:320, game:280, edit:280};
export const minimumDockHeight = tree => !tree ? 0 : tree.type === 'leaf'
  ? dockHeights[tree.view] ?? 240
  : tree.axis === 'y' ? minimumDockHeight(tree.a)+minimumDockHeight(tree.b)+8 : Math.max(minimumDockHeight(tree.a),minimumDockHeight(tree.b));
const columns = tree => tree?.type === 'split' && tree.axis === 'x' ? [...columns(tree.a),...columns(tree.b)] : tree ? [tree] : [];
const sceneColumn = tree => views(tree).every(view=>['game','edit'].includes(view));
const toolColumn = tree => views(tree).every(view=>!['node','lua','game','edit'].includes(view));
function join(a,b,axis) {
  const ratio=axis==='y'?minimumDockHeight(a)/(minimumDockHeight(a)+minimumDockHeight(b)):minimumWidth(a)/(minimumWidth(a)+minimumWidth(b));
  return {type:'split',axis,ratio,id:`adaptive:${axis}:${views(a).join(',')}|${views(b).join(',')}`,a,b};
}
function projected(tree,width,height,ratios) {
  if(tree.type==='leaf')return tree;
  const id=`adaptive:${tree.axis}:${views(tree.a).join(',')}|${views(tree.b).join(',')}`;
  const axis=tree.axis, space=(axis==='x'?width:height)-8;
  const min=axis==='x'?minimumWidth:minimumDockHeight;
  const preferred=ratios[id] ?? (axis==='y'?min(tree.a)/(min(tree.a)+min(tree.b)):tree.ratio);
  const ratio=Math.max(min(tree.a)/space,Math.min(1-min(tree.b)/space,preferred));
  return {...tree,id,ratio,a:projected(tree.a,axis==='x'?space*ratio:width,axis==='y'?space*ratio:height,ratios),b:projected(tree.b,axis==='x'?space*(1-ratio):width,axis==='y'?space*(1-ratio):height,ratios)};
}
// Reflow adjacent columns only; preserve leaf order and the user's canonical tree.
export function responsivePresentation(tree,width,height,ratios={}) {
  if(!stackLayout(tree,width))return {mode:'dock',tree:fitDockTree(tree,width),height};
  if(width<1040 || !tree)return {mode:'stack',tree,height};
  const groups=columns(tree);
  const total=()=>groups.reduce((sum,group)=>sum+minimumWidth(group),0)+Math.max(0,groups.length-1)*8;
  while(total()>width && groups.length>1) {
    let best=0, bestScore=Infinity;
    for(let i=0;i<groups.length-1;i++) {
      const a=groups[i],b=groups[i+1];
      const score=sceneColumn(a)&&sceneColumn(b)?0:toolColumn(a)&&toolColumn(b)?1:10;
      if(score<bestScore){best=i;bestScore=score;}
    }
    groups.splice(best,2,join(groups[best],groups[best+1],'y'));
  }
  if(total()>width)return {mode:'stack',tree,height};
  const merged=groups.reduce((a,b)=>a?join(a,b,'x'):b,null);
  const nextHeight=Math.max(height,minimumDockHeight(merged));
  return {mode:'adaptive',tree:projected(merged,width,nextHeight,ratios),height:nextHeight};
}
