export const defaultSettings = () => ({ project: 'Bir gece, bir ışık', resolution: '1280x800', startScene: 'selected', grid: true, guides: true });
export const RESOLUTIONS = { '1280x800': [1280,800], '1920x1080': [1920,1080], '1080x1920': [1080,1920] };
export function normalizeSettings(value) {
  const defaults = defaultSettings();
  if (!value || typeof value !== 'object') return defaults;
  return {
    project: typeof value.project === 'string' && value.project.trim() ? value.project.trim().slice(0,80) : defaults.project,
    resolution: Object.hasOwn(RESOLUTIONS,value.resolution) ? value.resolution : defaults.resolution,
    startScene: ['selected','0','1','2'].includes(value.startScene) ? value.startScene : defaults.startScene,
    grid: typeof value.grid === 'boolean' ? value.grid : defaults.grid,
    guides: typeof value.guides === 'boolean' ? value.guides : defaults.guides
  };
}
export function editObject(object, field, value) {
  if (!['x','y','w','h'].includes(field) || !Number.isFinite(value)) return object;
  const next = { ...object, [field]: value };
  next.w = Math.max(2,Math.min(100,next.w)); next.h = Math.max(2,Math.min(100,next.h));
  next.x = Math.max(0,Math.min(100-next.w,next.x)); next.y = Math.max(0,Math.min(100-next.h,next.y));
  return next;
}
