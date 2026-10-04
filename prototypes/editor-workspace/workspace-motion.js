// Animate flex allocations, so the windows keep disjoint layout areas throughout.
export function workspaceMotion(root, panels) {
  let animations = [], timer = null, complete = null;
  const reduced = () => matchMedia('(prefers-reduced-motion: reduce)').matches;
  function cancel() {
    clearTimeout(timer); timer = null;
    for (const animation of animations) animation.cancel();
    animations = [];
    const finish = complete; complete = null;
    if (finish) finish();
  }
  function animate(element, frames, duration=200, fill='none') {
    const animation=element.animate(frames,{duration,easing:'cubic-bezier(.22,1,.36,1)',fill});
    animations.push(animation);
    animation.finished.then(()=>{if(fill==='none') animations=animations.filter(item=>item!==animation);},()=>{});
  }
  function capture() {
    return {rects:new Map([...panels].filter(([,panel])=>panel.isConnected).map(([view,panel])=>[view,panel.getBoundingClientRect()])),height:root.clientHeight};
  }
  function extent(element, before, axis) {
    const boxes=[...element.querySelectorAll('[data-panel]')].map(panel=>before.get(panel.dataset.panel)).filter(Boolean);
    if (!boxes.length) return 0;
    return axis==='x'?Math.max(...boxes.map(r=>r.right))-Math.min(...boxes.map(r=>r.left)):Math.max(...boxes.map(r=>r.bottom))-Math.min(...boxes.map(r=>r.top));
  }
  function enter(snapshot) {
    if(reduced()) return;
    const before=snapshot.rects;
    if(Math.abs(snapshot.height-root.clientHeight)>1) animate(root,[{height:`${snapshot.height}px`},{height:`${root.clientHeight}px`}]);
    for(const split of root.querySelectorAll('.split')) {
      const [a,,b]=split.children, axis=split.dataset.axis;
      const first=extent(a,before,axis), second=extent(b,before,axis), total=first+second;
      if(!total) continue;
      const target=parseFloat(a.style.flex), start=Math.max(.001,Math.min(.999,first/total));
      if(Math.abs(start-target)<.001) continue;
      animate(a,[{flexGrow:start},{flexGrow:target}]);
      animate(b,[{flexGrow:1-start},{flexGrow:1-target}]);
    }
    for(const [view,panel] of panels) if(panel.isConnected&&!before.has(view)) {
      animate(panel.querySelector('.panel-content'),[{opacity:0,transform:'translateY(6px)'},{opacity:1,transform:'translateY(0)'}]);
    }
  }
  function exit(removed, visible, finish, height) {
    if(reduced()||!removed.length){finish();return;}
    if(Math.abs(root.clientHeight-height)>1) animate(root,[{height:`${root.clientHeight}px`},{height:`${height}px`}],180,'forwards');
    for(const view of removed){const panel=panels.get(view);if(panel?.isConnected)animate(panel.querySelector('.panel-content'),[{opacity:1},{opacity:0}],100,'forwards');}
    for(const split of root.querySelectorAll('.split')) {
      const [a,,b]=split.children;
      const remains=element=>[...element.querySelectorAll('[data-panel]')].some(panel=>visible.includes(panel.dataset.panel));
      const hasA=remains(a), hasB=remains(b);
      if(hasA===hasB)continue;
      animate(a,[{flexGrow:parseFloat(a.style.flex)},{flexGrow:hasA?1:0}],180,'forwards');
      animate(b,[{flexGrow:parseFloat(b.style.flex)},{flexGrow:hasB?1:0}],180,'forwards');
    }
    complete=finish;
    timer=setTimeout(()=>{timer=null;complete=null;for(const animation of animations)animation.cancel();animations=[];finish();},180);
  }
  return {cancel,capture,enter,exit};
}
