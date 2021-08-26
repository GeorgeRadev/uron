# uron

small stateless v8 based microservice that uses postgres and redis as storage layer.
development in progress.

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