// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using Jupiter.Common.Implementation;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using OpenTelemetry.Trace;

namespace Jupiter.Tests.Unit
{
	[TestClass]
	public class CompressedBufferTests
	{

		[TestMethod]
		public async Task CompressAndDecompressAsync()
		{
			byte[] bytes = Encoding.UTF8.GetBytes("this is a test string");

			Tracer tracer = TracerProvider.Default.GetTracer("TestTracer");
			BufferedPayloadOptions bufferedPayloadOptions = new BufferedPayloadOptions()
			{
			};
			IOptionsMonitor<BufferedPayloadOptions> payloadOptionsMock = Mock.Of<IOptionsMonitor<BufferedPayloadOptions>>(_ => _.CurrentValue == bufferedPayloadOptions);
			BufferedPayloadFactory bufferedPayloadFactory = new BufferedPayloadFactory(payloadOptionsMock, tracer);
			CompressedBufferUtils bufferUtils = new(tracer, bufferedPayloadFactory);

			using MemoryStream ms = new MemoryStream();
			IoHash uncompressedHash = bufferUtils.CompressContent(ms, OoodleCompressorMethod.Mermaid, OoodleCompressionLevel.VeryFast, bytes);
			ms.Position = 0;

			(IBufferedPayload, IoHash) decompressResult = await bufferUtils.DecompressContentAsync(ms, (ulong)ms.Length);
			using IBufferedPayload bufferedPayload = decompressResult.Item1;
			await using Stream s = bufferedPayload.GetStream();
			byte[] roundTrippedBytes = await s.ReadAllBytesAsync();
			CollectionAssert.AreEqual(bytes, roundTrippedBytes);
		}
	}
}
