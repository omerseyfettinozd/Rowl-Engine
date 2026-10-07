export const SCENARIOS=['normal','long','crowded','hub-empty','hub-many','loading','error','success','dirty','disabled','field-error','no-results'];
export const selectScenario = value => SCENARIOS.includes(value)?value:null;
export const activeScenario=selectScenario(new URLSearchParams(typeof location==='undefined'?'':location.search).get('review'));
export function makeDesignData(scenario,source) {
  const data=structuredClone(source);
  data.format='rowl-authoring-prototype/v1';data.scripts={};data.assets=[];
  if(['long','crowded'].includes(scenario)) {
    const count=scenario==='crowded'?30:data.scenes.length;
    data.scenes=Array.from({length:count},(_,i)=>({...structuredClone(source.scenes[i%source.scenes.length]),id:`review-${i}`,title:i===0?'Ay ışığında başlayan ve çok uzun bir hikâyeye dönüşen yolculuğun unutulmaz ilk gecesi':`Sahne ${i+1} · Yolculuk`,dialogue:i===0?('Gece ne kadar sessiz… Ama anlatılacak daha çok şey var.\n').repeat(50).slice(0,2000):source.scenes[i%source.scenes.length].dialogue}));
    data.scripts=Object.fromEntries(Array.from({length:scenario==='crowded'?20:3},(_,i)=>[i===0?'gece-yolculugunun-uzun-adli-karakter-davranislari.lua':`sahne-${i+1}.lua`,`-- Sahne ${i+1}\nlocal scene = {}\nreturn scene\n`]));
    const types=['dialogue','background','character','audio','choice','variable','condition','script','camera','transition'];
    data.scenes[0].objects.character.components=types.map(type=>({type,values:{source:'Assets/Images/ay.png',speaker:'Mira',text:data.scenes[0].dialogue,fontSize:18}}));
    data.assets=Array.from({length:scenario==='crowded'?50:3},(_,i)=>({name:i===0?'ay-isiginda-yolculuk-icin-hazirlanmis-cok-uzun-adli-arka-plan-gorseli.png':`Görsel-${String(i+1).padStart(2,'0')}.png`,type:'image/png',size:1024+i}));
  }
  data.projects=scenario==='hub-empty'?[]:Array.from({length:scenario==='hub-many'?12:1},(_,i)=>({id:`project-${i}`,name:i===0?data.settings.project:`Ay ışığında uzun bir yolculuk ve unutulmayan hikâyeler ${i+1}`,nodes:3+i}));
  return data;
}
