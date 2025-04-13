const char fileman[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>File Manager</title>
<style>
div,table{border-radius: 3px;box-shadow: 2px 2px 12px #000000;
background: rgb(94,94,94);
background: linear-gradient(0deg, rgba(94,94,94,1) 0%, rgba(160,160,160,1) 100%);
background-clip: padding-box;}
input{border-radius: 5px;box-shadow: 2px 2px 12px #000000;
background: rgb(160,160,160);
background: linear-gradient(0deg, rgba(160,160,160,1) 0%, rgba(239,239,239,1) 100%);
background-clip: padding-box;}
body{width:470px;display:block;margin-left:auto;margin-right:auto;font-family: Arial, Helvetica, sans-serif;}
</style>
<script src="http://ajax.googleapis.com/ajax/libs/jquery/1.6.1/jquery.min.js" type="text/javascript" charset="utf-8"></script>
<script type="text/javascript">
a=document.all
path='/'
idx=0
var myToken=localStorage.getItem('myStoredText1')
$(document).ready(function(){
  openSocket()
})

function openSocket(){
 ws=new WebSocket("ws://"+window.location.host+"/ws")
// ws=new WebSocket("ws://192.168.31.24/ws")
 ws.onopen=function(evt){}
 ws.onclose=function(evt){alert("Connection closed.");}
 ws.onmessage=function(evt){
 console.log(evt.data)
  d=JSON.parse(evt.data)
  if(d.cmd=='state'){
  }
  else if(d.cmd=='settings')
  {
    a.free.innerHTML=d.currfs+': '+(d.diskfree/1024).toFixed()+' KB Free'
    if(d.sdavail) a.sdcard.disabled=false
  }
  else if(d.cmd=='files')
  {
    a.path.innerHTML=path
    d.list.sort(function(a1, b1) {
      n=a1[0].toLowerCase().localeCompare(b1[0].toLowerCase())
      if(a1[2]) n-=10
      if(b1[2]) n+=10
      return n
    });
    a.fileList.innerHTML = ''
    if(d.list.length == 0)
      a.fileList.innerHTML = 'Drop Files Here'
    idx=0
    for(i=0;i<d.list.length;i++)
     AddFile(d.list[i])
  }
  else if(d.cmd=='alert'){alert(d.text)}
 }
}

function setVar(varName, value)
{
 ws.send('{"key":"'+myToken+'","'+varName+'":'+value+'}')
}

function AddFile(item)
{
  tr=document.createElement("tr")
  td=document.createElement("td")
  inp=document.createElement("input")
  inp.id=idx
  if(item[0]=='..'){
   inp.type='button'
   inp.width=90
  }else{
   inp.type='image'
   inp.value=item[0]
   inp.onclick=function(){delfile(this.id,this.value)}
  }
  inp.src='del-btn.png'
  td.appendChild(inp)
  tr.appendChild(td)

  td=document.createElement("td")
  td.style.textAlign = "left"
  td.value=item[0]
  td.innerHTML=' &nbsp; '+item[0].substr(0,31)+' '
  if(item[2]&1){
   td.style.color = "yellow"
   td.onclick=function(){cd(this.value)}
  }else
    td.onclick=function(){download(this.value)}
  tr.appendChild(td)

  td=document.createElement("td")
  if((item[2]&1)==0){
    if(item[1]>1024)
      td.innerHTML=' '+(item[1]/1024).toFixed()+'K'
    else
      td.innerHTML=' '+item[1]
  }
  tr.appendChild(td)

  tbody=document.createElement("tbody")
  tbody.id='tbody'+idx
  tbody.appendChild(tr)
  a.fileList.appendChild(tbody)
  idx++
}

function cd(p)
{
  if(p=='..'){
    pathParts=path.split('/')
    pathParts.pop()
    path=pathParts.join('/')
  }else{
    if(path.length>1) path+='/'
    path+=p
  }
  setVar('cd', path)
}

function delfile(idx,name)
{
  setVar('delf', '"'+fullName(name)+'"')
  l=document.getElementById('tbody'+idx)
  a.fileList.removeChild(l)
}

function fullName(name)
{
  if(path.length>1) return path+'/'+name
  else return '/'+name
}
function download(name){
  const b = document.createElement('a')
  b.href=encodeURIComponent(fullName(name))
  b.download = name
  document.body.appendChild(b)
  b.click()
  document.body.removeChild(b)
}
</script>
<style type="text/css">
input{
 border-radius: 5px;
 margin-bottom: 5px;
 box-shadow: 2px 2px 12px #000000;
 background-clip: padding-box;
}
.range{
  overflow: hidden;
}
.btn {
  background-color: #50a0ff;
  padding: 1px;
  font-size: 12px;
  min-width: 50px;
  border: none;
}
</style>
</head>
<body bgcolor="silver">
<table>
<tr><td><input type=button value="Internal" onClick="setVar('setfs','int');">
<input id=sdcard disabled type=button value="SD Card" onClick="setVar('setfs','SD');">
</td>
</tr>
<tr><td>
<p align="left" id="free">Internal: 0K Free</p>
<p align="left" id="path"></p>
</td></tr>
<tr>
<td>
<div id="dropContainer">
<table style="font-size:small" id="fileList">
<tr><td>Drop Files Here</td></tr>
</table>
</div>
</td>
</tr>
</table>
</body>
<script type="text/javascript">
function appendToPath(orig, add) {
  return orig.replace(/\/$/, "") + "/" + add.replace(/^\//, "");
}
dropContainer.ondragover = dropContainer.ondragenter = function(ev){ev.preventDefault()}
dropContainer.ondragend = dropContainer.ondraleave = function(ev){ev.preventDefault()}

dropContainer.ondrop = function(evt) {
  data=evt.dataTransfer.files[0]
  evt.preventDefault()
  item=evt.dataTransfer.items[0].webkitGetAsEntry()
  it=new Array()
  it[0]=data.name
  it[1]=0
  it[2]=0
  if(item.isDirectory)
  {
    it[2]=1
    setVar('createdir','"'+fullName(data.name)+'"')
    AddFile(it)
    return
  }
  it[1]=data.size
  formData = new FormData()
  formData.append(data.name,data,fullName(data.name))
  fetch('/upload', {method: 'POST', body: formData})
  AddFile(it)
}
</script>
</html>
)rawliteral";
