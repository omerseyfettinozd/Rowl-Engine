export function renderStatus(host,kind,{title,detail,action,label}={}) {
  host.replaceChildren();host.className=`design-status status-${kind}`;
  host.setAttribute('role',kind==='error'?'alert':'status');host.setAttribute('aria-busy',String(kind==='loading'));
  const icon=document.createElement('span');icon.className='status-symbol';icon.setAttribute('aria-hidden','true');icon.textContent={loading:'◌',error:'!',success:'✓',empty:'○',dirty:'●',disabled:'—'}[kind]??'○';
  const heading=document.createElement('strong'),text=document.createElement('p');heading.textContent=title;text.textContent=detail;host.append(icon,heading,text);
  if(label){const button=document.createElement('button');button.className='author-button';button.textContent=label;button.disabled=kind==='disabled';if(action)button.onclick=action;host.append(button);}
}
let fieldId=0;
export function bindFormFeedback(form) {
  function clear(input){input.removeAttribute('aria-invalid');const error=form.querySelector(`[data-error-for="${input.dataset.feedbackId}"]`);if(error)error.hidden=true;}
  form.addEventListener('invalid',event=>{
    const input=event.target;event.preventDefault();input.dataset.feedbackId??=`field-${++fieldId}`;
    const id=`${input.dataset.feedbackId}-error`;let error=form.querySelector(`#${id}`);
    if(!error){error=document.createElement('span');error.id=id;error.dataset.errorFor=input.dataset.feedbackId;error.className='field-error';error.setAttribute('role','alert');input.after(error);input.setAttribute('aria-describedby',[input.getAttribute('aria-describedby'),id].filter(Boolean).join(' '));}
    const v=input.validity;error.textContent=v.valueMissing?'Bu alanı doldur.':v.rangeOverflow?`En fazla ${input.max} gir.`:v.rangeUnderflow?`En az ${input.min} gir.`:v.patternMismatch?'İstenen biçime uygun bir değer gir.':'Geçerli bir değer gir.';error.hidden=false;input.setAttribute('aria-invalid','true');
    const invalid=form.querySelector('[aria-invalid="true"]');invalid?.focus();
  },true);
  form.addEventListener('input',event=>{if(event.target.validity?.valid)clear(event.target);});
}
