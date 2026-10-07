import {changeSceneZoom,sceneGeometry,resizedScroll,readingPresentation} from './scene-view-state.mjs';
export function bindSceneView(panel,name) {
  const area=panel.querySelector('.scene-area'),stage=area.querySelector('.scene-stage-wrap'),frame=stage.querySelector('.game-frame');
  const canvas=document.createElement('div');canvas.className='scene-canvas';canvas.append(frame);stage.append(canvas);
  stage.tabIndex=0;stage.setAttribute('role','region');stage.setAttribute('aria-label',`${name} sahne gezinmesi`);
  const reader=document.createElement('section');reader.className='scene-reading';reader.hidden=true;reader.setAttribute('aria-label',`${name} okuma alanı`);
  reader.innerHTML='<strong></strong><p></p>';reader.tabIndex=0;
  const body=document.createElement('div');body.className='scene-body';stage.before(body);body.append(stage,reader);
  const controls=document.createElement('div');controls.className='scene-controls';
  controls.innerHTML=`<button class="scene-control" data-scene-zoom="out" aria-label="${name} sahnesini uzaklaştır">−</button><span class="scene-zoom-value">100%</span><button class="scene-control" data-scene-zoom="in" aria-label="${name} sahnesini yakınlaştır">+</button><button class="scene-control" data-scene-zoom="fit" aria-label="${name} sahnesini ekrana sığdır" title="Sığdır"><svg><use href="#i-fit"/></svg></button><button class="scene-control scene-read-toggle" aria-label="${name} metnini oku" aria-expanded="false">Oku</button>`;
  area.querySelector('.scene-bottom').append(controls);
  let zoom=1,aspect=1/1.6,previous=null;
  function fit(nextAspect=aspect) {
    aspect=nextAspect;
    const mode=readingPresentation(body.clientWidth,body.clientHeight,!reader.hidden);
    body.dataset.reading=mode;stage.inert=mode==='overlay';
    stage.setAttribute('aria-hidden',String(mode==='overlay'));
    for(const button of controls.querySelectorAll('[data-scene-zoom]'))button.hidden=mode==='overlay';
    controls.querySelector('.scene-zoom-value').hidden=mode==='overlay';
    controls.querySelector('.scene-read-toggle').textContent=reader.hidden?'Oku':'Kapat';
    controls.querySelector('.scene-read-toggle').setAttribute('aria-label',reader.hidden?`${name} metnini oku`:`${name} okuma alanını kapat`);
    if(!stage.isConnected || !stage.clientWidth || !stage.clientHeight)return;
    const next=sceneGeometry(stage.clientWidth,stage.clientHeight,aspect,zoom);
    const x=resizedScroll(previous?.canvasWidth??next.canvasWidth,next.canvasWidth,stage.clientWidth,stage.scrollLeft);
    const y=resizedScroll(previous?.canvasHeight??next.canvasHeight,next.canvasHeight,stage.clientHeight,stage.scrollTop);
    canvas.style.width=`${next.canvasWidth}px`;canvas.style.height=`${next.canvasHeight}px`;
    frame.style.width=`${next.frameWidth}px`;frame.style.height=`${next.frameHeight}px`;
    stage.scrollLeft=x;stage.scrollTop=y;previous=next;
    controls.querySelector('.scene-zoom-value').textContent=`${Math.round(zoom*100)}%`;
    controls.querySelector('[data-scene-zoom="out"]').disabled=zoom===1;
    controls.querySelector('[data-scene-zoom="in"]').disabled=zoom===3;
  }
  controls.addEventListener('click',event=>{
    const button=event.target.closest('[data-scene-zoom]');
    if(button){zoom=changeSceneZoom(zoom,button.dataset.sceneZoom);fit();}
    if(event.target.closest('.scene-read-toggle')){reader.hidden=!reader.hidden;controls.querySelector('.scene-read-toggle').setAttribute('aria-expanded',String(!reader.hidden));fit();if(innerHeight<=500)panel.scrollIntoView({block:'start'});}
  });
  return {fit,update(scene){reader.querySelector('strong').textContent=scene.speaker||'Mira';reader.querySelector('p').textContent=scene.dialogue;}};
}
