FROM drogonframework/drogon@sha256:6e6855f2fac69c394b574f23cde70e88a23f7453bc8a0be67d4c2e7c5f1e2fca AS build

WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBLOG_BUILD_TESTS=ON \
    && cmake --build build --parallel \
    && ctest --test-dir build --output-on-failure

FROM drogonframework/drogon@sha256:6e6855f2fac69c394b574f23cde70e88a23f7453bc8a0be67d4c2e7c5f1e2fca
WORKDIR /app
COPY --from=build /src/build/drogon_blog /app/drogon_blog
COPY --from=build /src/public /app/public
COPY --from=build /src/content /app/content
COPY --from=build /src/config/config.docker.json /app/config/config.docker.json
USER 65532:65532
EXPOSE 8080
CMD ["/app/drogon_blog", "config/config.docker.json"]
