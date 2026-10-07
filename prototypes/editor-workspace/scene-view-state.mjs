export const changeSceneZoom = (zoom,action) => action==='fit'?1:Math.max(1,Math.min(3,Math.round((zoom+(action==='in'?.25:-.25))*4)/4));
export function sceneGeometry(width,height,aspect,zoom=1) {
  const w=Math.max(0,width),h=Math.max(0,height),base=Math.min(w,h/aspect);
  const frameWidth=base*zoom,frameHeight=frameWidth*aspect;
  return {frameWidth,frameHeight,canvasWidth:Math.max(w,frameWidth),canvasHeight:Math.max(h,frameHeight)};
}
export const resizedScroll = (oldSize,newSize,viewport,scroll) => Math.max(0,Math.min(newSize-viewport,oldSize?((scroll+viewport/2)/oldSize)*newSize-viewport/2:0));
export const percentDelta = (dx,dy,width,height) => ({x:width?dx/width*100:0,y:height?dy/height*100:0});
export function graphGeometry(viewWidth,viewHeight,worldWidth,worldHeight,compact,zoom=1) {
  const scale=Math.max(.75,compact?Math.min((viewWidth-20)/worldWidth,1):Math.min((viewWidth-24)/worldWidth,(viewHeight-20)/worldHeight,1.25))*zoom;
  const width=worldWidth*scale,height=worldHeight*scale;
  return {scale,width:Math.max(viewWidth,width),height:Math.max(viewHeight,height),left:Math.max(0,(viewWidth-width)/2),top:Math.max(0,(viewHeight-height)/2)};
}

export const readingPresentation = (width,height,open) => !open?'closed':width>=700 && height>=280?'side':height>=440?'inline':'overlay';
