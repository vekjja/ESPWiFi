#
# Production dashboard image
#
# - Builds the React app with Create React App (react-scripts)
# - Optionally uses an env file (default: .env.prod) during the build
# - Serves the resulting static bundle via nginx
#
# Expected env vars (in .env.prod):
#   REACT_APP_API_HOST=espwifi.local
#   REACT_APP_API_PORT=80
#   REACT_APP_API_PROTOCOL=http:
#   REACT_APP_WS_PROTOCOL=ws:
#
# Build:
#   docker build -t espwifi-dashboard:prod --build-arg ENV_FILE=.env.prod .
#

FROM node:22-alpine AS build

WORKDIR /app

COPY package.json package-lock.json ./
RUN npm install --no-audit --no-fund

COPY . .

# If you provide an env file in the build context (default: .env.prod),
# copy it into CRA's production env filename so react-scripts will load it.
ARG ENV_FILE=.env.prod
RUN if [ -f "$ENV_FILE" ]; then cp "$ENV_FILE" .env.production; fi

# Build to /app/build (the package.json "build" script targets ../data for firmware)
ENV GENERATE_SOURCEMAP=false
ENV BUILD_PATH=/app/build
RUN ./node_modules/.bin/react-scripts build

FROM nginx:1.27-alpine AS runtime

COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build /app/build /usr/share/nginx/html

EXPOSE 3000
CMD ["nginx", "-g", "daemon off;"]