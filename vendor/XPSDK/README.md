# X-Plane SDK

The X-Plane SDK headers and Windows link libraries are **not** committed to this repository. Download them manually before building locally.

## How to install

1. Go to https://developer.x-plane.com/sdk/
2. Download **SDK 4.x** (latest).
3. Extract the zip so this directory contains:
   ```
   vendor/XPSDK/
     CHeaders/
       XPLM/
         XPLMPlugin.h
         XPLMProcessing.h
         ...
       Wrappers/
       XPWidgets/
     Libraries/
       Win/
         XPLM_64.lib
         XPWidgets_64.lib
       Mac/   (unused — macOS links via -undefined dynamic_lookup)
   ```

## CI

GitHub Actions downloads the SDK automatically via the `Get X-Plane SDK` workflow step. See `.github/workflows/ci.yml`.

## License

The X-Plane SDK is copyright Laminar Research. Headers may be redistributed with plugin source per the SDK EULA; check the current terms at developer.x-plane.com before redistribution.
