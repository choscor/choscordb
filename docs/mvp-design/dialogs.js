/* Export and preferences stay in the current workspace as modal tools. */
function openExportDialog() {
  const template = document.createElement('template');
  template.innerHTML = exports();
  const panel = template.content.querySelector('.wide-narrow > .card');
  const start = panel.querySelector('[data-action="start-export"]');
  const cancel = panel.querySelector('[data-action="cancel-export"]');
  const error = template.content.querySelector('[data-action="export-error"]');
  cancel.disabled = true;
  const actions = error.outerHTML + btn('Close', 'close-modal') + cancel.outerHTML + start.outerHTML;
  start.closest('.row.space').remove();
  cancel.remove();
  panel.querySelector('h2').remove();
  const doc=page==='workspace'?editorDocuments.find(d=>d.id===activeDocumentId):null;
  const conn=explorerConnections.find(c=>c.id===doc?.connection)||selectedConnection;
  panel.querySelector('p').textContent=(doc?.title||'Sample results')+' · '+conn.name;
  panel.className = 'export-dialog-body';
  modal('Export results', panel.outerHTML, actions);
  document.querySelector('.modal').classList.add('export-dialog');
  document.getElementById('export-format').focus();
}
function openPreferencesDialog(section = 'appearance') {
  const template = document.createElement('template');
  template.innerHTML = settings();
  const tabs = template.content.querySelector('.menu-list');
  tabs.className = 'pane-tabs preference-tabs'; tabs.removeAttribute('style');
  const sections = [...template.content.querySelectorAll('section[id]')];
  sections.forEach(el => {el.removeAttribute('style');el.className='preference-section';el.querySelector('h2')?.remove();el.querySelector(':scope > p')?.remove()});
  modal('Preferences', tabs.outerHTML + `<div class="preferences-body">${sections.map(el => el.outerHTML).join('')}</div>`, btn('Close','close-modal')+btn('Save preferences','save-preferences','primary'));
  const root = document.querySelector('.modal'); root.classList.add('preferences-dialog');
  root.querySelectorAll('input,select').forEach((el,i)=>{
    const value=saved.preferences?.[el.id||'field'+i];
    if(value!==undefined){if(el.type==='checkbox')el.checked=value;else el.value=value}
  });
  root.querySelector('#theme').value=saved.theme||'Light';
  root.querySelectorAll('.preference-tabs a').forEach(a=>a.addEventListener('click',e=>{e.preventDefault();showPreferenceSection(a.hash.slice(1))}));
  showPreferenceSection(section);
  root.querySelector('.preference-tabs a.active').focus();
}
function showPreferenceSection(id) {
  const root=document.querySelector('.preferences-dialog');
  const sections=[...root.querySelectorAll('.preference-section')];
  if(!sections.some(el=>el.id===id))id='appearance';
  sections.forEach(el=>el.hidden=el.id!==id);
  root.querySelectorAll('.preference-tabs a').forEach(a=>{a.classList.toggle('active',a.hash==='#'+id);a.setAttribute('aria-current',a.hash==='#'+id?'page':'false')});
}
function applyEditorPreferences(){
  const editor=document.getElementById('sql-editor');if(!editor)return;
  const prefs=saved.preferences||{};
  editor.style.fontSize=(prefs['editor-size']||13)+'px';
  editor.style.fontFamily={'System monospace':'var(--mono)',Menlo:'Menlo, monospace',Consolas:'Consolas, monospace'}[prefs['editor-font']]||'var(--mono)';
  document.querySelector('.line-numbers').style.visibility=prefs['line-numbers']===false?'hidden':'visible';
}
function openConnectionActions(){
  modal(esc(selectedConnection.name),'<p class="small">'+esc(selectedConnection.driver)+' · '+esc(selectedConnection.database)+'</p><div class="menu-list">'+btn('Edit / test connection…','edit-selected-connection','ghost')+btn('Duplicate connection','duplicate-selected-connection','ghost')+btn('Delete connection…','delete-selected-connection','ghost danger')+'</div>');
}
function storeConnectionProfiles(){
  saved.extraConnections=explorerConnections.filter(c=>!['commerce','analytics','sandbox'].includes(c.id));
  saved.connectionOverrides=Object.fromEntries(explorerConnections.filter(c=>['commerce','analytics','sandbox'].includes(c.id)).map(c=>[c.id,c]));saved.explorerConnection=selectedConnection.id;persist();
}
document.addEventListener('click',e=>{
  const action=e.target.closest('[data-action]')?.dataset.action;
  if(action==='edit-selected-connection'){
    const profile=selectedConnection;openConnection(profile.driver==='SQLite');editingConnectionId=profile.id;
    document.getElementById('profile-name').value=profile.name;
    const db=document.getElementById('database-name')||document.getElementById('database-path');db.value=profile.database;
    document.getElementById('modal-title').textContent='Edit connection';
  }
  if(action==='duplicate-selected-connection'){
    selectedConnection={...selectedConnection,id:'profile-'+Date.now(),name:selectedConnection.name+' · copy'};
    explorerConnections.push(selectedConnection);storeConnectionProfiles();closeModal();document.querySelector('.sidebar').outerHTML=sidebar();toast('Connection duplicated without credentials.');
  }
  if(action==='delete-selected-connection')modal('Delete connection?',`<p>Remove ${esc(selectedConnection.name)} from this workspace? The database itself is unchanged.</p>`,btn('Cancel','close-modal')+btn('Delete connection','confirm-delete-selected','danger'));
  if(action==='confirm-delete-selected'){
    if(explorerConnections.length===1){closeModal();toast('Keep one sample connection, or add another before deleting this one.');return}
    saved.hiddenConnections=[...(saved.hiddenConnections||[]),selectedConnection.id];
    explorerConnections.splice(explorerConnections.indexOf(selectedConnection),1);selectedConnection=explorerConnections[0];storeConnectionProfiles();closeModal();document.querySelector('.sidebar').outerHTML=sidebar();toast('Connection removed.');
  }
});
