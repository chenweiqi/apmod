//Sorry for my poor English

Usage:
eg: http://www.example.com/js/??js1.js,js2.js,js3.js

Modules Setting:
open apache's config "httpd.conf"
LoadModule concatx_module modules/mod_concatx.dll

Advanced Settings:(Default value)
<IfModule concatx_module>
ConcatxDisable Off
ConcatxCheckModified On
ConcatxSeparator On
ConcatxMaxSize 1024
ConcatxMaxCount 10
ConcatxFileType js,css
</IfModule>

Details:
ConcatxDisable On/Off
Whether to disable mod_concatx

ConcatxCheckModified On/Off
Detects whether a file change, suggestion: On

ConcatxSeparator On/Off
Whether add newline when concat files, suggestion: On

ConcatxMaxSize 1024
Limit the max size of all files, suggest lower

ConcatxMaxCount 10
Limit the max number of all files, suggest lower

ConcatxFileType js,css
File types, if without limiting fill ","

