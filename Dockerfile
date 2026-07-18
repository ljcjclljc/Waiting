FROM drogonframework/drogon:latest AS build

WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBLOG_BUILD_TESTS=ON \
    && cmake --build build --parallel \
    && ctest --test-dir build --output-on-failure

FROM drogonframework/drogon:latest
WORKDIR /app
COPY --from=build /src/build/drogon_blog /app/drogon_blog
COPY --from=build /src/public /app/public
COPY --from=build /src/content /app/content
COPY --from=build /src/config/config.docker.json /app/config/config.docker.json
EXPOSE 8080
CMD ["/app/drogon_blog", "config/config.docker.json"]
