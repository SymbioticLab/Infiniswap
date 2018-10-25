import requests
url = 'http://admin:admin@localhost:3000/api/datasources'
headers = {"Content-Type": 'application/json','Accept': 'application/json','Authorization': 'Bearer eyJrIjoiT0tTcG1pUlY2RnVKZTFVaDFsNFZXdE9ZWmNrMkZYbk'}
data = '''
{
    "id":1,
    "orgId":1,
    "name":"Infiniswap",
    "type":"mysql",
    "typeLogoUrl":"public/app/plugins/datasource/mysql/img/mysql_logo.svg",
    "access":"proxy",
    "url":"",
    "password":"mysql",
    "user":"root",
    "database":"infiniswap",
    "host":"localhost:3306",
    "basicAuth":false,
    "isDefault":false,
    "jsonData":{"keepCookies":[]},
    "readOnly":false}
}
'''
response = requests.post(url,data=data, headers=headers)
print(response)
print(response.status_code)
print (response.text)
# f = open("output", "w")
# f.write(response.text)