# uron

small stateless v8 based microservice that uses postgres and redis as storage layer.
development in progress.

## V8 SQL patch

V8 SQL patch is in the **v8_sql** folder.  
It gives a nice sintaxis for creating postgres SQL expressions directly in the JavaScript with string and variable escaping to avoid SQL injections.  

for example the following REST handler:

```
export default async function requestHandler(request, response) {
    const username = "username"
    const sql = SELECT user WHERE user = :username AND role = 'test';
    
    response.setStatus(200);
    response.setContentType('text/plain');
    response.send(JSON.stringify(sql));
}
```

Where: **const sql = SELECT user WHERE user = 'test';**
Will be equivalent to:

```
     const sql = ["SELECT user WHERE user = $1 AND role = $2 ","username","test"]
```



## build


```
    mkdir -p build
    cmake build
    cmake --build ./build/
```


## setting up environment

Environment requires POSTGRES and REDIS:

first time:
```
    # docker system prune --all # if you need to clean docker
    docker pull postgres:alpine
    docker pull redis:alpine
    docker run --name postgres-local -e POSTGRES_PASSWORD=postgres -d postgres:alpine # u:postgres p:postgres
    docker run --name redis-local -d redis:alpine
```

for stopping and statring POSTGRES and REDIS:

```
    docker stop redis-local
    docker stop postgres-local
    docker start redis-local
    docker start postgres-local
```

## starting an instance

```
    mkdir -p cache
    ./build/uron CACHE=./cache DB=  REDIS=
```