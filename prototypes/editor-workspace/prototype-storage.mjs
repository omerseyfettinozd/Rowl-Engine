import {activeScenario} from './design-scenarios.mjs';
export function createPrototypeStorage(backing,isolated=false) {
  const memory=new Map();
  return {getItem:key=>isolated?(memory.get(key)??null):backing.getItem(key),setItem(key,value){if(isolated)memory.set(key,String(value));else backing.setItem(key,value);}};
}
export const prototypeStorage=createPrototypeStorage({getItem:key=>localStorage.getItem(key),setItem:(key,value)=>localStorage.setItem(key,value)},activeScenario!==null);
