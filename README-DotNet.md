.NET extension for NORM
==========================

By:  
  Jeff Muller <jeffrey.muller3.ctr@us.navy.mil>  
  Sylvester Freeman <sylvester.freeman18.civ@us.navy.mil>  
  Sergiy Yermak <sergiy.yermak.ctr@us.navy.mil>  

The .NET extension for NORM provides a .NET wrapper for the NORM C API.

For documentation about the main NORM API calls, refer to the NORM Developers
guide in the regular NORM distribution.

The .NET extension can be built using the .NET CLI.

------------
Requirements
------------

The .NET extension for NORM requires at least .NET SDK 6.0.

The NORM library should be built prior to building the .NET extension since it is invoked by the .NET extension.

------------
Building
------------

To build the .NET extension for NORM, execute the .NET CLI command in the src/dotnet directory:

  ```
  dotnet build .
  ```

------------
Testing
------------

To test the .NET extension for NORM, execute the .NET CLI command in the src/dotnet directory:

  ```
  dotnet test .
  ```

The test command results should show that all tests have passed.
Some tests might be skipped due to IO exception.

------------
Packaging
------------
To package the .NET extension for NORM, execute the .NET CLI command in the src/dotnet directory:

```
dotnet pack . -c Release
```
