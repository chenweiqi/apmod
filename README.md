# apmod
copy my old project  from code.google.com/p/apmod

## mod_concatx
mod_concatx: the ability to join multiple files together in a single request.
This is a performance optimization. instead of requesting several seperate css or javascript files from your server, you can do it one request.
It is based on mod_concat but makes ​​improvements.

1. use the browser cache in effect
2. avoid service code leak
3. avoid content stick together
4. support gzip

### Usage
```html
<script src="http://www.example.com/js/??js1.js,js2.js,js3.js" ></script>
```

### Apache Module Setting
modify config: "conf/httpd.conf"
```plain
LoadModule concatx_module modules/mod_concatx.dll
```

### Advanced Settings:(Default)
```plain
<IfModule concatx_module>
ConcatxDisable Off
ConcatxCheckModified On
ConcatxSeparator On
ConcatxMaxSize 1024
ConcatxMaxCount 10
ConcatxFileType js,css
</IfModule>
```

### Setting Meaning
#### ConcatxDisable On/Off
Whether to disable mod_concatx

#### ConcatxCheckModified On/Off
Detects whether a file change, suggestion: On

#### ConcatxSeparator On/Off
Whether add newline when concat files, suggestion: On

#### ConcatxMaxSize 1024
Limit the max size of all files, suggest lower

#### ConcatxMaxCount 10
Limit the max number of all files, suggest lower

#### ConcatxFileType js,css
File types, if without limiting fill ","


### Support gzip
This feature depend on mod_deflate, one of native apache modules
so modify apache config "conf/httpd.conf", and add:
```plain
LoadModule deflate_module modules/mod_deflate.so
```
