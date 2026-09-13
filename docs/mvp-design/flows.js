/* Navigation and document state shared across the remaining screens. */
let editorDocuments = [], activeDocumentId = null, documentsInitialized = false;
function snapshotEditor() {
  if (!documentsInitialized) return;
  const doc = editorDocuments.find(d => d.id === activeDocumentId);
  const editor = document.getElementById('sql-editor');
  if (doc && editor) doc.sql = editor.innerText;
  saved.editorDocuments = editorDocuments;
  saved.activeDocumentId = activeDocumentId;
  persist();
}
function initializeWorkspaceDocuments() {
  if (page !== 'workspace' || documentsInitialized) return;
  documentsInitialized = true;
  const editor = document.getElementById('sql-editor');
  editorDocuments = Array.isArray(saved.editorDocuments) ? saved.editorDocuments.filter(d => d && typeof d.sql === 'string' && typeof d.id === 'string') : [];
  if (!editorDocuments.length) editorDocuments = [
    {id:'customer-overview', title:'Customer overview', sql:editor.innerText, connection:'commerce'},
    {id:'monthly-revenue', title:'Monthly revenue', sql:"SELECT date_trunc('month', created_at), SUM(total)\nFROM public.orders\nGROUP BY 1;", connection:'commerce'}
  ];
  activeDocumentId = editorDocuments.some(d=>d.id===saved.activeDocumentId) ? saved.activeDocumentId : editorDocuments[0].id;
  if (window.incomingDocument) {
    const incoming = window.incomingDocument;
    const doc = {id:'query-'+Date.now(),title:incoming.title||'Opened query',sql:incoming.sql,connection:incoming.connection||selectedConnection.id};
    editorDocuments.push(doc); activeDocumentId=doc.id; delete window.incomingDocument;
  }
  showEditorDocument(activeDocumentId, false);
}
function showEditorDocument(id, capture = true) {
  if (capture) snapshotEditor();
  const doc=editorDocuments.find(d=>d.id===id);if(!doc)return;
  activeDocumentId=id;
  document.getElementById('sql-editor').innerHTML=esc(doc.sql).replace(/--[^\n]*|'[^']*'|\b(?:SELECT|FROM|WHERE|ORDER|BY|GROUP|LIMIT|INSERT|INTO|VALUES|UPDATE|SET|DELETE|DESC|AS|SUM)\b|\b\d+\b/gi,token=>`<span class="${token.startsWith('--')?'comment':token.startsWith("'")?'str':/^\d/.test(token)?'num':'kw'}">${token}</span>`);
  const connection=explorerConnections.find(c=>c.id===doc.connection)||selectedConnection;
  document.querySelector('.editor-bottom>.small').textContent=connection.name+' / '+connection.schema+' · '+connection.driver;
  document.querySelector('.work-tabs').innerHTML=editorDocuments.map(d=>`<div class="document-tab ${d.id===id?'active':''}"><button data-flow="switch-tab" data-document="${esc(d.id)}" role="tab" aria-selected="${d.id===id}">${icon('code')}${esc(d.title)}</button><button class="tab-close" data-flow="close-tab" data-document="${esc(d.id)}" aria-label="Close ${esc(d.title)}">${icon('close')}</button></div>`).join('')+btn(icon('plus'),'new-query','ghost');
  document.querySelector('.work-tabs').setAttribute('role','tablist');
  document.getElementById('data-panel').classList.toggle('hidden',!doc.hasResults);
  document.getElementById('messages-panel').classList.add('hidden');
  document.getElementById('result-view').value='data';
  queryState(doc.hasResults?'24 ms · Completed':'Ready · not executed');
  applyEditorPreferences();snapshotEditor();
}
function newEditorDocument() {
  initializeWorkspaceDocuments();snapshotEditor();
  const doc={id:'query-'+Date.now(),title:'Untitled query',sql:'-- Start with a question\nSELECT ',connection:selectedConnection.id};
  editorDocuments.push(doc);showEditorDocument(doc.id,false);document.getElementById('sql-editor').focus();
}
function recordQueryCompletion(sql) {
  const doc=editorDocuments.find(d=>d.id===activeDocumentId);if(doc)doc.hasResults=true;snapshotEditor();
  if(saved.preferences?.['save-history']===false)return;
  saved.executedQueries=[{sql,title:doc?.title||'Query',connection:doc?.connection||selectedConnection.id,at:new Date().toLocaleTimeString(),status:'Completed'},...(saved.executedQueries||[])].slice(0,100);persist();
}
function initializeFlow() {
  initializeWorkspaceDocuments();
  if(page==='history'&&saved.executedQueries?.length){
    const list=document.getElementById('history-list');
    for(const record of [...saved.executedQueries].reverse()){
      const item=document.createElement('article');item.className='history-item';item.dataset.status='Completed';item.dataset.connection=record.connection;item.tabIndex=0;item.setAttribute('role','button');item.setAttribute('aria-pressed','false');
      const conn=explorerConnections.find(c=>c.id===record.connection);
      item.innerHTML=`<div class="row"><span class="mono">${esc(record.sql)}</span></div><div class="row small muted">${badge('Completed')}<span>${esc(record.at)}</span><span>${esc(conn?.name||record.connection)}</span><span>24 ms · 8 sample rows</span></div>`;
      item.addEventListener('click',()=>selectHistoryItem(item));item.addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();selectHistoryItem(item)}});list.prepend(item);
    }
    selectHistoryItem(list.firstElementChild);
  }
  if(page==='schema'){
    const table=new URLSearchParams(location.search).get('table')||(saved.activeObject?.connection===selectedConnection.id?saved.activeObject.table:selectedConnection.tables[0])||'customers';saved.activeObject={connection:selectedConnection.id,table};persist();
    const actions=document.querySelector('.page-actions');
    actions.insertAdjacentHTML('afterbegin',btn('Open query','open-object-query')+btn(icon('export')+' Export','open-export')+link('Review changes','changes','button'));
  }
  if(page==='changes'){
    const actions=document.querySelector('.page-actions');
    if(!actions.querySelector('a[href="schema.html"]'))actions.insertAdjacentHTML('afterbegin',link('Back to object','schema','button'));
  }
}
function queueObjectQuery(sql, title) {
  saved.pendingSQL=sql;saved.pendingTitle=title;saved.pendingConnection=selectedConnection.id;persist();location.href='workspace.html';
}
function qualifiedObject() {
  const table=new URLSearchParams(location.search).get('table')||(saved.activeObject?.connection===selectedConnection.id?saved.activeObject.table:selectedConnection.tables[0])||'customers';
  const safe=/^[a-zA-Z_][a-zA-Z0-9_]*$/.test(table)?table:'customers';
  return selectedConnection.schema+'.'+safe;
}
document.addEventListener('click',e=>{
  const flow=e.target.closest('[data-flow]');
  if(flow?.dataset.flow==='switch-tab'){if(running)toast('Finish or cancel the active query before switching tabs.');else showEditorDocument(flow.dataset.document);}
  if(flow?.dataset.flow==='close-tab'){
    if(running){toast('Finish or cancel the active query before closing a tab.');return}
    snapshotEditor();editorDocuments=editorDocuments.filter(d=>d.id!==flow.dataset.document);
    if(!editorDocuments.length){saved.editorDocuments=[];saved.activeDocumentId=null;documentsInitialized=false;persist();location.href='index.html';return}
    showEditorDocument(editorDocuments.some(d=>d.id===activeDocumentId)?activeDocumentId:editorDocuments.at(-1).id,false);
  }
  const action=e.target.closest('[data-action]')?.dataset.action;
  if(action==='open-object-query')queueObjectQuery('SELECT * FROM '+qualifiedObject()+' LIMIT 1000;',qualifiedObject());
  if(action==='select-connection'&&page==='index')setTimeout(()=>{
    const first=selectedConnection.tables[0];
    if(first)location.href='schema.html?table='+encodeURIComponent(first);
    else queueObjectQuery('-- '+selectedConnection.name+'\nSELECT ', 'Untitled query');
  });
});
document.addEventListener('input',e=>{if(e.target.id==='sql-editor')snapshotEditor()});
window.addEventListener('pagehide',snapshotEditor);
