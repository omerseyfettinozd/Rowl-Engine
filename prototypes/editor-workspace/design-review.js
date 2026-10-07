import {renderStatus,bindFormFeedback} from './design-status.js';
export function mountReviewState(scenario,panels,authoring) {
  if(scenario.startsWith('hub-')){authoring.openHub();return;}
  if(['normal','long','crowded'].includes(scenario))return;
  const original=panels.get('inspector').querySelector('.tool-content');for(const child of original.children)child.hidden=true;
  const content=document.createElement('div');content.className='review-state';original.prepend(content);
  const caption=document.createElement('p');caption.className='tool-hint';caption.textContent='Tasarım örneği · gerçek bir işlem yürütülmüyor.';content.append(caption);
  const host=document.createElement('section');content.append(host);
  const show=kind=>{
    const values={loading:{title:'İçerik hazırlanıyor',detail:'Liste hazırlanırken çalışma alanında kalabilirsin.',label:'Örneği tamamla',action:()=>show('success')},error:{title:'İçerik getirilemedi',detail:'Taslağın korunuyor. İşlemi yeniden deneyebilirsin.',label:'Yeniden dene',action:()=>show('loading')},success:{title:'İşlem tamamlandı',detail:'İçerik hazır. Çalışmaya devam edebilirsin.'},dirty:{title:'Kaydedilmemiş değişiklikler',detail:'Bu tasarım örneğindeki değişiklikler henüz kaydedilmedi.',label:'Örneği kaydet',action:()=>show('success')},disabled:{title:'Önce bir öğe seç',detail:'Bir öğe seçildiğinde bu işlem kullanılabilir.',label:'Seçileni düzenle'},'no-results':{title:'Eşleşme bulunamadı',detail:'Aramayı temizle veya farklı bir isim dene.',label:'Aramayı temizle',action:()=>show('success')}};
    renderStatus(host,kind==='no-results'?'empty':kind,values[kind]);
  };
  if(scenario!=='field-error'){show(scenario);return;}
  const form=document.createElement('form');form.className='review-form';form.innerHTML='<label class="author-field">Örnek adı<input name="title" required aria-label="Örnek adı"></label><label class="author-field">Örnek süre (1–10)<input name="duration" type="number" min="1" max="10" value="20" required aria-label="Örnek süre"></label><button class="author-button">Uygula</button>';
  bindFormFeedback(form);form.onsubmit=event=>{event.preventDefault();renderStatus(host,'success',{title:'Alanlar geçerli',detail:'Örnek değerler kabul edildi.'});};content.append(form);requestAnimationFrame(()=>requestAnimationFrame(()=>{form.reportValidity();form.querySelector('[aria-invalid="true"]')?.scrollIntoView({block:'nearest'});}));
}
