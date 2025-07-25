<?xml version="1.0" encoding="utf-8"?>
<TpsData xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <Name>nccl</Name>
  <!-- Software Name and Version  -->
<!-- Software Name: nccl
    Download Link: https://github.com/iree-org/iree/tree/candidate-20241104.1068/third_party/nccl
    Version: 2.18.1
    Notes: It is part of IREE. The software is compiled into binaries which are used as a compiler for the neural networks. It is distributed with the engine but not in games. The software is also compiled into static runtime libraries to load and run the compiled neural networks in Editor and also in games. All dependencies are compiled into the binaries or the static libraries.
        -->
<Location>Engine/Plugins/Experimental/NNERuntimeIREE/Source/ThirdParty/IREE/</Location>
<Function>It is part of IREE. The binaries are used to compile neural network models inside the Editor while the static libraries are used both in Editor as well as in games to load and run the models.</Function>
<Eula>https://github.com/iree-org/iree/blob/candidate-20241104.1068/third_party/nccl/LICENSE.txt</Eula>
  <RedistributeTo>
    <EndUserGroup>Licensee</EndUserGroup>
    <EndUserGroup>P4</EndUserGroup>
    <EndUserGroup>Git</EndUserGroup>
  </RedistributeTo>
  <LicenseFolder>/Engine/Source/ThirdParty/Licenses</LicenseFolder>
</TpsData>
 