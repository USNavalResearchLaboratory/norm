using Bogus;
using Mil.Navy.Nrl.Norm.Enums;
using System.Text;

namespace Mil.Navy.Nrl.Norm.IntegrationTests
{
    public class NormSessionTests : IDisposable
    {
        private readonly NormInstance _normInstance;
        private NormSession _normSession;
        private bool _isInstanceDestroyed;
        private bool _isSessionDestroyed;
        private bool _isSenderStarted;
        private bool _isSenderStopped;

        private bool _isReceiverStarted;
        private bool _isReceiverStopped;

        private string _testPath;

        /// <summary>
        /// Create a NORM session
        /// </summary>
        private NormSession CreateSession()
        {
            var faker = new Faker();
            var sessionAddress = "224.1.2.3";
            var sessionPort = faker.Internet.Port();
            var localNodeId = NormNode.NORM_NODE_ANY;

            return _normInstance.CreateSession(sessionAddress, sessionPort, localNodeId);
        }

        private void CreateTestDirectory()
        {
            if (!Directory.Exists(_testPath))
            {
                Directory.CreateDirectory(_testPath);
            }
        }

        private void DeleteTestDirectory()
        {
            if (Directory.Exists(_testPath))
            {
                Directory.Delete(_testPath, true);
            }
        }

        public NormSessionTests()
        {
            _normInstance = new NormInstance();
            _normSession = CreateSession();
            _isInstanceDestroyed = false;
            _isSessionDestroyed = false;
            _isSenderStarted = false;
            _isSenderStopped = false;
            var currentDirectory = Directory.GetCurrentDirectory();
            _testPath = Path.Combine(currentDirectory, Guid.NewGuid().ToString());
            CreateTestDirectory();
        }

        private void StartSender()
        {
            if (!_isSenderStarted)
            {
                _normSession.StartSender(1024 * 1024, 1400, 64, 16);
                _isSenderStarted = true;
            }
        }

        private void StopSender()
        {
            if (_isSenderStarted && !_isSenderStopped)
            {
                _normSession.StopSender();
                _isSenderStopped = true;
            }
        }

        private void StartReceiver()
        {
            if (!_isReceiverStarted)
            {
                //The appropriate bufferSpace to use is a function of expected network delay * bandwidth product and packet loss characteristics
                _normSession.StartReceiver(10 * 10);
                _isReceiverStarted = true;
            }
        }

        private void StopReceiver()
        {
            if (_isReceiverStarted && !_isReceiverStopped)
            {
                _normSession.StopReceiver();
                _isReceiverStopped = true;
            }
        }

        /// <summary>
        /// Destroys the NORM session
        /// </summary>
        private void DestroySession()
        {
            if (!_isSessionDestroyed)
            {
                StopSender();
                _normSession.DestroySession();
                _isSessionDestroyed = true;
            }
        }

        /// <summary>
        /// Destroy the NORM instance
        /// </summary>
        private void DestroyInstance()
        {
            if (!_isInstanceDestroyed)
            {
                DestroySession();
                _normInstance.DestroyInstance();
                _isInstanceDestroyed = true;
            }
        }

        /// <summary>
        /// Dispose destroys the NORM instance
        /// </summary>
        public void Dispose()
        {
            DestroyInstance();
            DeleteTestDirectory();
        }

        /// <summary>
        /// Test for creating a NORM session
        /// </summary>
        [Fact]
        public void CreatesSession()
        {
            Assert.NotNull(_normSession);
        }

        /// <summary>
        /// Test for destroying a NORM session
        /// </summary>
        [Fact]
        public void DestroysSession()
        {
            DestroySession();
        }

        /// <summary>
        /// Test for starting a NORM sender
        /// </summary>
        [SkippableFact(typeof(IOException))]
        public void StartsSender()
        {
            StartSender();
        }

        /// <summary>
        /// Test for stopping a NORM sender
        /// </summary>
        [SkippableFact(typeof(IOException))]
        public void StopsSender()
        {
            StartSender();
            StopSender();
        }

        [SkippableFact(typeof(IOException))]
        public void StartsReceiver()
        {
            StartReceiver();
        }

        [SkippableFact(typeof(IOException))]
        public void StopsReceiver()
        {
            StartReceiver();
            StopReceiver();
        }

        /// <summary>
        /// Generates text content
        /// </summary>
        /// <returns>The generated text content</returns>
        private string GenerateTextContent()
        {
            var faker = new Faker();
            return faker.Lorem.Paragraph();
        }

        private IEnumerable<NormEvent> GetEvents(TimeSpan delayTime)
        {
            var normEvents = new List<NormEvent>();
            while (_normInstance.HasNextEvent(delayTime))
            {
                var normEvent = _normInstance.GetNextEvent(false);
                if (normEvent != null)
                {
                    normEvents.Add(normEvent);
                }
            }
            return normEvents;
        }

        private IEnumerable<NormEvent> GetEvents()
        {
            return GetEvents(TimeSpan.FromMilliseconds(30));
        }

        private void WaitForEvents(TimeSpan delayTime)
        {
            GetEvents(delayTime);
        }

        private void WaitForEvents()
        {
            GetEvents();
        }

        private void AssertNormEvents(IEnumerable<NormEvent> normEvents)
        {
            foreach (var normEvent in normEvents)
            {
                var normNode = normEvent.Node;
                if (normNode != null)
                {
                    var actualId = normNode.Id;
                    Assert.NotEqual(NormNode.NORM_NODE_NONE, actualId);
                    var actualAddress = normNode.Address;
                    Assert.NotNull(actualAddress);
                    var actualGrtt = normNode.Grtt;
                    Assert.NotEqual(-1, actualGrtt);
                    var expectedEventString = $"NormEvent [type={normEvent.Type}]";
                    var actualEventString = normEvent.ToString();
                    Assert.Equal(expectedEventString, actualEventString);
                }
            }
        }

        [SkippableFact(typeof(IOException))]
        public void EnqueuesFile()
        {
            StartSender();

            var fileContent = GenerateTextContent();
            var fileName = Guid.NewGuid().ToString();
            var filePath = Path.Combine(_testPath, fileName);
            File.WriteAllText(filePath, fileContent);

            try
            {
                var normFile = _normSession.FileEnqueue(filePath);
                Assert.NotNull(normFile);
                var expectedEventTypes = new List<NormEventType> { NormEventType.NORM_TX_OBJECT_SENT, NormEventType.NORM_TX_QUEUE_EMPTY };
                var actualEvents = GetEvents();
                var actualEventTypes = actualEvents.Select(e => e.Type).ToList();
                Assert.Equal(expectedEventTypes, actualEventTypes);
                var expectedFileName = filePath;
                var actualFileName = normFile.Name;
                Assert.Equal(expectedFileName, actualFileName);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void ReceivesFile()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();

            //Set up cache directory
            var folderName = Guid.NewGuid().ToString();
            var cachePath = Path.Combine(_testPath, folderName);
            Directory.CreateDirectory(cachePath);
            _normInstance.SetCacheDirectory(cachePath);

            //Set up file to send
            var fileName = Guid.NewGuid().ToString();
            var fileContent = GenerateTextContent();
            var filePath = Path.Combine(_testPath, fileName);
            File.WriteAllText(filePath, fileContent);

            try
            {
                //Enqueue file
                var normFile = _normSession.FileEnqueue(filePath);
                //Wait for events
                var normEvents = GetEvents();
                AssertNormEvents(normEvents);

                var expectedNormEventType = NormEventType.NORM_RX_OBJECT_COMPLETED;
                Assert.Contains(expectedNormEventType, normEvents.Select(e => e.Type));
                var normObjectEvent = normEvents.First(e => e.Type == expectedNormEventType);

                var receivedNormFile = Assert.IsType<NormFile>(normObjectEvent.Object);
                var expectedFileName = receivedNormFile.Name;

                //Check that file exists
                var expectedFileCount = 1;
                var actualFiles = Directory.GetFiles(cachePath);
                var actualFileCount = actualFiles.Length;
                Assert.Equal(expectedFileCount, actualFileCount);

                //Check file content
                var actualFileName = actualFiles.First();
                Assert.Equal(expectedFileName, actualFileName);
                var actualContent = File.ReadAllText(actualFileName);
                Assert.Equal(fileContent, actualContent);

            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void ReceivesFileWithRename()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();

            //Set up cache directory
            var folderName = Guid.NewGuid().ToString();
            var cachePath = Path.Combine(_testPath, folderName);
            Directory.CreateDirectory(cachePath);
            _normInstance.SetCacheDirectory(cachePath);

            //Set up file to send
            var fileName = Guid.NewGuid().ToString();
            var fileContent = GenerateTextContent();
            var filePath = Path.Combine(_testPath, fileName);
            File.WriteAllText(filePath, fileContent);

            try
            {
                //Enqueue file
                var fileNameBytes = Encoding.ASCII.GetBytes(fileName);
                var normFile = _normSession.FileEnqueue(filePath, fileNameBytes, 0, fileNameBytes.Length);
                //Wait for events
                var normEvents = GetEvents();
                AssertNormEvents(normEvents);

                var expectedNormEventType = NormEventType.NORM_RX_OBJECT_COMPLETED;
                Assert.Contains(expectedNormEventType, normEvents.Select(e => e.Type));
                var normObjectEvent = normEvents.First(e => e.Type == expectedNormEventType);

                var receivedNormFile = Assert.IsType<NormFile>(normObjectEvent.Object);
                var actualInfo = receivedNormFile.Info;
                Assert.NotNull(actualInfo);

                var expectedFileName = fileName;
                var actualFileName = Encoding.ASCII.GetString(actualInfo);
                Assert.Equal(expectedFileName, actualFileName);

                var expectedFilePath = Path.Combine(cachePath, actualFileName);
                receivedNormFile.Rename(expectedFilePath);

                //Check that file exists
                var expectedFileCount = 1;
                var actualFiles = Directory.GetFiles(cachePath);
                var actualFileCount = actualFiles.Length;
                Assert.Equal(expectedFileCount, actualFileCount);

                //Check file content
                var actualFilePath = actualFiles.First();
                Assert.Equal(expectedFilePath, actualFilePath);
                var actualContent = File.ReadAllText(actualFilePath);
                Assert.Equal(fileContent, actualContent);

            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void EnqueuesData()
        {
            StartSender();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                var expectedEventTypes = new List<NormEventType> { NormEventType.NORM_TX_OBJECT_SENT, NormEventType.NORM_TX_QUEUE_EMPTY };
                var actualEventTypes = GetEvents().Select(e => e.Type).ToList();
                Assert.Equal(expectedEventTypes, actualEventTypes);
                var actualData = normData.GetData();
                Assert.Equal(expectedData, actualData);
                var actualContent = Encoding.ASCII.GetString(actualData);
                Assert.Equal(expectedContent, actualContent);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void ReceivesData()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();

            //Set up cache directory
            var folderName = Guid.NewGuid().ToString();
            var cachePath = Path.Combine(_testPath, folderName);
            Directory.CreateDirectory(cachePath);
            _normInstance.SetCacheDirectory(cachePath);

            //Create data to be sent
            var expectedContent = GenerateTextContent();
            var expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                var expectedEventTypes = new List<NormEventType>
                {
                    NormEventType.NORM_REMOTE_SENDER_NEW,
                    NormEventType.NORM_REMOTE_SENDER_ACTIVE,
                    NormEventType.NORM_TX_OBJECT_SENT,
                    NormEventType.NORM_TX_QUEUE_EMPTY,
                    NormEventType.NORM_RX_OBJECT_NEW,
                    NormEventType.NORM_RX_OBJECT_UPDATED,
                    NormEventType.NORM_RX_OBJECT_COMPLETED
                };
                var actualEvents = GetEvents();
                AssertNormEvents(actualEvents);

                var actualEventTypes = actualEvents.Select(e => e.Type).ToList();
                Assert.Equivalent(expectedEventTypes, actualEventTypes);

                var actualEvent = actualEvents.FirstOrDefault(e => e.Type == NormEventType.NORM_RX_OBJECT_COMPLETED);
                var actualNormData = Assert.IsType<NormData>(actualEvent?.Object);
                
                var actualData = actualNormData.GetData();
                Assert.Equal(expectedData, actualData);
                var actualContent = Encoding.ASCII.GetString(actualData);
                Assert.Equal(expectedContent, actualContent);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void SendsStream()
        {
            StartSender();

            var fileContent = GenerateTextContent();
            var data = Encoding.ASCII.GetBytes(fileContent);
            var dataOffset = 0;
            NormStream? normStream = null;

            try
            {
                var repairWindowSize = 1024 * 1024;
                normStream = _normSession.StreamOpen(repairWindowSize);

                var expectedBytesWritten = data.Length;
                var actualBytesWritten = normStream.Write(data,  dataOffset, data.Length);

                WaitForEvents();
                normStream.MarkEom();
                normStream.Flush();

                Assert.Equal(expectedBytesWritten, actualBytesWritten);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                normStream?.Close(true);
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void ReceivesStream()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();

            //Set up cache directory
            var folderName = Guid.NewGuid().ToString();
            var cachePath = Path.Combine(_testPath, folderName);
            Directory.CreateDirectory(cachePath);
            _normInstance.SetCacheDirectory(cachePath);

            var fileContent = GenerateTextContent();
            var data = Encoding.ASCII.GetBytes(fileContent);
            var dataOffset = 0;
            NormStream? normStream = null;

            try
            {
                var repairWindowSize = 1024 * 1024;
                normStream = _normSession.StreamOpen(repairWindowSize);

                var expectedBytesWritten = data.Length;
                normStream.Write(data, dataOffset, data.Length-dataOffset);

                normStream.MarkEom();
                normStream.Flush();
                var normEvents = GetEvents();
                AssertNormEvents(normEvents);

                var expectedNormEventType = NormEventType.NORM_RX_OBJECT_UPDATED;
                Assert.Contains(expectedNormEventType, normEvents.Select(e => e.Type));
                var normObjectEvent = normEvents.First(e => e.Type == expectedNormEventType);

                var receivedNormStream = Assert.IsType<NormStream>(normObjectEvent.Object);
                var numRead = 0;
                var receiveBuffer = new byte[65536];
                while ((numRead = receivedNormStream.Read(receiveBuffer, dataOffset, receiveBuffer.Length-dataOffset)) > 0)
                {
                    if (numRead != -1)
                    {
                        var receivedData = receiveBuffer.Take(numRead).ToArray();
                        var receivedContent = Encoding.ASCII.GetString(receivedData);
                        Assert.Equal(fileContent, receivedContent);
                    }
                }
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                normStream?.Close(true);
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void ReceivesStreamWithOffset()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();

            //Set up cache directory
            var folderName = Guid.NewGuid().ToString();
            var cachePath = Path.Combine(_testPath, folderName);
            Directory.CreateDirectory(cachePath);
            _normInstance.SetCacheDirectory(cachePath);

            var fileContent = GenerateTextContent();
            var data = Encoding.ASCII.GetBytes(fileContent);
            NormStream? normStream = null;

            try
            {
                var repairWindowSize = 1024 * 1024;

                normStream = _normSession.StreamOpen(repairWindowSize);
                var offset = 0;
                var length = data.Length;
                normStream.Write(data, offset, length);
                normStream.MarkEom();
                normStream.Flush();

                var normEvents = GetEvents();
                AssertNormEvents(normEvents);

                var expectedNormEventType = NormEventType.NORM_RX_OBJECT_UPDATED;
                var normObjectEvent = normEvents.First(e => e.Type == expectedNormEventType);
                var receivedNormStream = Assert.IsType<NormStream>(normObjectEvent.Object);

                var receiveBuffer = new byte[65536];
                var receiveOffset = 0;
                var receiveBufferLength = 10;
                var totalNumRead = 0;
                var numRead = 0;

                while ((numRead = receivedNormStream.Read(receiveBuffer, receiveOffset, receiveBufferLength)) > 0)
                {
                    if (numRead != -1)
                    {
                        totalNumRead += numRead;
                        receiveOffset += numRead;
                    }
                }
                var receivedData = receiveBuffer.Take(totalNumRead).ToArray();
                var receivedContent = Encoding.ASCII.GetString(receivedData);
                Assert.Equal(fileContent, receivedContent);
               
              
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                normStream?.Close(true);
                StopSender();
                StopReceiver();
            }
        }

        [Fact]
        public void SetsTxPort()
        {
            _normSession.SetTxPort(6003);
        }

        [Fact]
        public void SetsRxPortReuseTrue()
        {
            _normSession.SetRxPortReuse(true);
        }

        [Fact]
        public void SetsRxPortReuseFalse()
        {
            _normSession.SetRxPortReuse(false);
        }

        [Fact]
        public void SetsEcnSupport()
        {
            _normSession.SetEcnSupport(true, true);
        }

        [Fact]
        public void SetsMulticastInterface()
        {
            _normSession.SetMulticastInterface("interface_name");
        }

        [Fact]
        public void SetsSSM()
        {
            var sourceAddress = "224.1.2.3";
            _normSession.SetSSM(sourceAddress);
        }

        [Fact]
        public void SetsSSMThrowsIOException()
        {
            var sourceAddress = "999.999.999.999";
            Assert.Throws<IOException>(() => _normSession.SetSSM(sourceAddress));
        }

        [Fact]
        public void SetsTTL()
        {
            var ttl = (byte)200;
            _normSession.SetTTL(ttl);
        }

        [Fact]
        public void SetsTOS()
        {
            var tos = (byte)200;
            _normSession.SetTOS(tos);
        }

        [Fact]
        public void SetsMessageTrace()
        {
            var flag = true;
            _normSession.SetMessageTrace(flag);
        }

        [Fact]
        public void SetsTxLoss()
        {
            var txLoss = .50;
            _normSession.SetTxLoss(txLoss);
        }

        [Fact]
        public void SetsRxLoss()
        {
            var rxLoss = .50;
            _normSession.SetRxLoss(rxLoss);
        }

        [Fact]
        public void SetsReportInterval()
        {
            var expectedReportInterval = .50;
            _normSession.ReportInterval = expectedReportInterval;
            var actualReportInterval = _normSession.ReportInterval;
            Assert.Equal(expectedReportInterval, actualReportInterval);
        }

        [Fact]
        public void SetsTxOnly()
        {
            var txOnly = true;
            _normSession.SetTxOnly(txOnly);
        }

        [Fact]
        public void SetsFlowControl()
        {
            var flowControlFactor = .50;
            _normSession.SetFlowControl(flowControlFactor);
        }

        [Fact]
        public void SetsTxSocketBuffer()
        {
            StartSender();
            var bufferSize = 100;
            _normSession.SetTxSocketBuffer(bufferSize);
        }

        [Fact]
        public void SetsCongestionControl()
        {
            var enable = true;
            _normSession.SetCongestionControl(enable);
        }

        [Fact]
        public void SetsTxRateBounds()
        {
            var rateMin = 1.0;
            var rateMax = 100.0;
            _normSession.SetTxRateBounds(rateMin, rateMax);
        }

        [Fact]
        public void SetsTxCacheBounds()
        {
            var sizeMax = 100;
            var countMin = 1;
            var countMax = 99;

            _normSession.SetTxCacheBounds(sizeMax, countMin, countMax);
        }

        [Fact]
        public void SetsAutoParity()
        {
            short autoParity = 123;
            _normSession.SetAutoParity(autoParity);
        }

        [Fact]
        public void SetsTxRate()
        {
            double txRate = 20.0;
            _normSession.TxRate = txRate;
        }

        [Fact]
        public void GetsTxRate()
        {
            double expectedTxRate = 10.0;
            _normSession.TxRate = expectedTxRate;

            var actualTxRate = _normSession.TxRate;
            Assert.Equal(expectedTxRate, actualTxRate);
        }

        [Fact]
        public void SetsGrttEstimate()
        {
            double grttEstimate = 10.0;
            _normSession.GrttEstimate = grttEstimate;
        }

        [Fact]
        public void GetsGrttEstimate()
        {
            double expetedGrttEstimate = 10.0;
            _normSession.GrttEstimate = expetedGrttEstimate;

            var actualGrttEstimate = _normSession.GrttEstimate;
            Assert.InRange<double>(actualGrttEstimate, expetedGrttEstimate - 1.0, expetedGrttEstimate + 1.0);
        }

        [Fact]
        public void SetsGrttMax()
        {
            double grttMax = 20.0;
            _normSession.SetGrttMax(grttMax);
        }

        [Fact]
        public void SetsGrttProbingMode_NORM_PROBE_NONE()
        {
            var probingMode = NormProbingMode.NORM_PROBE_NONE;
            _normSession.SetGrttProbingMode(probingMode);
        }

        [Fact]
        public void SetsGrttProbingMode_NORM_PROBE_PASSIVE()
        {
            var probingMode = NormProbingMode.NORM_PROBE_PASSIVE;
            _normSession.SetGrttProbingMode(probingMode);
        }

        [Fact]
        public void SetsGrttProbingMode_NORM_PROBE_ACTIVE()
        {
            var probingMode = NormProbingMode.NORM_PROBE_ACTIVE;
            _normSession.SetGrttProbingMode(probingMode);
        }

        [Fact]
        public void SetsGrttProbingInterval()
        {
            var intervalMin = 1.0;
            var intervalMax = 10.0;
            _normSession.SetGrttProbingInterval(intervalMin, intervalMax);
        }

        [Fact]
        public void SetsBackoffFactor()
        {
            var backoffFactor = 10.0;
            _normSession.SetBackoffFactor(backoffFactor);
        }

        [Fact]
        public void SetsGroupSize()
        {
            var groupSize = 100;
            _normSession.SetGroupSize(groupSize);
        }

        [Fact]
        public void SetsTxRobustFactor()
        {
            var txRobustFactor = 2;
            _normSession.SetTxRobustFactor(txRobustFactor);
        }

        [SkippableFact(typeof(IOException))]
        public void RequeuesObject()
        {
            StartSender();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                var expectedEventTypes = new List<NormEventType> { NormEventType.NORM_TX_OBJECT_SENT, NormEventType.NORM_TX_QUEUE_EMPTY };
                var actualEventTypes = GetEvents().Select(e => e.Type).ToList();
                Assert.Equal(expectedEventTypes, actualEventTypes);
                _normSession.RequeueObject(normData);
                actualEventTypes = GetEvents().Select(e => e.Type).ToList();
                Assert.Equal(expectedEventTypes, actualEventTypes);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void SetsWatermark()
        {
            _normSession.AddAckingNode(_normSession.LocalNodeId);
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();

            //Set up cache directory
            var folderName = Guid.NewGuid().ToString();
            var cachePath = Path.Combine(_testPath, folderName);
            Directory.CreateDirectory(cachePath);
            _normInstance.SetCacheDirectory(cachePath);

            //Create data to be sent
            var expectedContent = GenerateTextContent();
            var expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                _normSession.SetWatermark(normData);
                var expectedEventTypes = new List<NormEventType>
                {
                    NormEventType.NORM_REMOTE_SENDER_NEW,
                    NormEventType.NORM_REMOTE_SENDER_ACTIVE,
                    NormEventType.NORM_TX_OBJECT_SENT,
                    NormEventType.NORM_TX_QUEUE_EMPTY,
                    NormEventType.NORM_RX_OBJECT_NEW,
                    NormEventType.NORM_RX_OBJECT_UPDATED,
                    NormEventType.NORM_RX_OBJECT_COMPLETED,
                    NormEventType.NORM_TX_WATERMARK_COMPLETED
                };
                var actualEvents = GetEvents(TimeSpan.FromSeconds(1));
                var actualEventTypes = actualEvents.Select(e => e.Type).ToList();
                Assert.Equivalent(expectedEventTypes, actualEventTypes);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void CancelsWatermark()
        {
            _normSession.AddAckingNode(_normSession.LocalNodeId);
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();

            //Set up cache directory
            var folderName = Guid.NewGuid().ToString();
            var cachePath = Path.Combine(_testPath, folderName);
            Directory.CreateDirectory(cachePath);
            _normInstance.SetCacheDirectory(cachePath);

            //Create data to be sent
            var expectedContent = GenerateTextContent();
            var expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                _normSession.SetWatermark(normData);
                _normSession.CancelWatermark();
                var expectedEventTypes = new List<NormEventType>
                {
                    NormEventType.NORM_REMOTE_SENDER_NEW,
                    NormEventType.NORM_REMOTE_SENDER_ACTIVE,
                    NormEventType.NORM_TX_OBJECT_SENT,
                    NormEventType.NORM_TX_QUEUE_EMPTY,
                    NormEventType.NORM_RX_OBJECT_NEW,
                    NormEventType.NORM_RX_OBJECT_UPDATED,
                    NormEventType.NORM_RX_OBJECT_COMPLETED
                };
                var actualEvents = GetEvents(TimeSpan.FromSeconds(1));
                var actualEventTypes = actualEvents.Select(e => e.Type).ToList();
                Assert.Equivalent(expectedEventTypes, actualEventTypes);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void ResetsWatermark()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();

            //Set up cache directory
            var folderName = Guid.NewGuid().ToString();
            var cachePath = Path.Combine(_testPath, folderName);
            Directory.CreateDirectory(cachePath);
            _normInstance.SetCacheDirectory(cachePath);

            //Create data to be sent
            var expectedContent = GenerateTextContent();
            var expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                _normSession.SetWatermark(normData);
                var expectedEventTypes = new List<NormEventType>
                {
                    NormEventType.NORM_REMOTE_SENDER_NEW,
                    NormEventType.NORM_REMOTE_SENDER_ACTIVE,
                    NormEventType.NORM_TX_OBJECT_SENT,
                    NormEventType.NORM_TX_QUEUE_EMPTY,
                    NormEventType.NORM_RX_OBJECT_NEW,
                    NormEventType.NORM_RX_OBJECT_UPDATED,
                    NormEventType.NORM_RX_OBJECT_COMPLETED
                };
                var actualEvents = GetEvents(TimeSpan.FromSeconds(1));
                var actualEventTypes = actualEvents.Select(e => e.Type).ToList();
                Assert.Equivalent(expectedEventTypes, actualEventTypes);

                _normSession.AddAckingNode(_normSession.LocalNodeId);
                _normSession.ResetWatermark();
                expectedEventTypes = new List<NormEventType>
                {
                    NormEventType.NORM_TX_WATERMARK_COMPLETED
                };
                actualEvents = GetEvents(TimeSpan.FromSeconds(1));
                actualEventTypes = actualEvents.Select(e => e.Type).ToList();
                Assert.Equivalent(expectedEventTypes, actualEventTypes);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [Fact]
        public void AddsAckingNode()
        {
            _normSession.AddAckingNode(_normSession.LocalNodeId);
        }

        [Fact]
        public void RemovesAckingNode()
        {
            _normSession.AddAckingNode(_normSession.LocalNodeId);
            _normSession.RemoveAckingNode(_normSession.LocalNodeId);
        }

        [SkippableFact(typeof(IOException))]
        public void GetsAckingStatus()
        {
            _normSession.AddAckingNode(_normSession.LocalNodeId);
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();

            //Set up cache directory
            var folderName = Guid.NewGuid().ToString();
            var cachePath = Path.Combine(_testPath, folderName);
            Directory.CreateDirectory(cachePath);
            _normInstance.SetCacheDirectory(cachePath);

            //Create data to be sent
            var expectedContent = GenerateTextContent();
            var expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                _normSession.SetWatermark(normData);
                WaitForEvents(TimeSpan.FromSeconds(1));
                var expectedAckingStatus = NormAckingStatus.NORM_ACK_SUCCESS;
                var actualAckingStatus = _normSession.GetAckingStatus(_normSession.LocalNodeId);
                Assert.Equal(expectedAckingStatus, actualAckingStatus);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void SendsCommand()
        {
            StartSender();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedCommand = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                _normSession.SendCommand(expectedCommand, expectedCommand.Length, false);
                var expectedEventTypes = new List<NormEventType> { NormEventType.NORM_TX_CMD_SENT };
                var actualEventTypes = GetEvents().Select(e => e.Type).ToList();
                Assert.Equal(expectedEventTypes, actualEventTypes);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void ReceivesCommand()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedCommand = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                _normSession.SendCommand(expectedCommand, expectedCommand.Length, false);
                var expectedEventTypes = new List<NormEventType> 
                { 
                    NormEventType.NORM_TX_CMD_SENT, 
                    NormEventType.NORM_REMOTE_SENDER_NEW,
                    NormEventType.NORM_REMOTE_SENDER_ACTIVE,
                    NormEventType.NORM_RX_CMD_NEW 
                };
                var actualEventTypes = GetEvents().Select(e => e.Type).ToList();
                Assert.Equivalent(expectedEventTypes, actualEventTypes);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void CancelsCommand()
        {
            StartSender();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedCommand = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                _normSession.SendCommand(expectedCommand, expectedCommand.Length, false);
                _normSession.CancelCommand();
                var expectedEventTypes = new List<NormEventType>();
                var actualEventTypes = GetEvents().Select(e => e.Type).ToList();
                Assert.Equal(expectedEventTypes, actualEventTypes);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [Fact]
        public void SetsRxCacheLimit()
        {
            var countMax = 5;
            _normSession.SetRxCacheLimit(countMax);
        }

        [SkippableFact(typeof(IOException))]
        public void SetsRxSocketBuffer()
        {
            StartSender();
            var bufferSize = 8;
            _normSession.SetRxSocketBuffer(bufferSize);
        }

        [Fact]
        public void SetsSilentReceiver()
        {
            var silent = true;
            _normSession.SetSilentReceiver(silent, -1);
        }

        [Fact]
        public void SetsDefaultUnicastNack()
        {
            var enable = true;
            _normSession.SetDefaultUnicastNack(enable);
        }

        [SkippableFact(typeof(IOException))]
        public void SetsUnicastNack()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedCommand = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                _normSession.SendCommand(expectedCommand, expectedCommand.Length, false);
                var normEventType = NormEventType.NORM_RX_CMD_NEW;
                var actualEvents = GetEvents();
                Assert.Contains(normEventType, actualEvents.Select(e => e.Type));
                var actualEvent = actualEvents.First(e => e.Type == normEventType);
                var actualNode = actualEvent.Node;
                Assert.NotNull(actualNode);
                actualNode.SetUnicastNack(true);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [Fact]
        public void SetsDefaultSyncPolicy_NORM_SYNC_CURRENT()
        {
            var syncPolicy = NormSyncPolicy.NORM_SYNC_CURRENT;
            _normSession.SetDefaultSyncPolicy(syncPolicy);
        }

        [Fact]
        public void SetsDefaultSyncPolicy_NORM_SYNC_STREAM()
        {
            var syncPolicy = NormSyncPolicy.NORM_SYNC_STREAM;
            _normSession.SetDefaultSyncPolicy(syncPolicy);
        }

        [Fact]
        public void SetsDefaultSyncPolicy_NORM_SYNC_ALL()
        {
            var syncPolicy = NormSyncPolicy.NORM_SYNC_ALL;
            _normSession.SetDefaultSyncPolicy(syncPolicy);
        }

        [Fact]
        public void SetsDefaultNackingMode_NORM_NACK_NONE()
        {
            var nackingMode = NormNackingMode.NORM_NACK_NONE;
            _normSession.SetDefaultNackingMode(nackingMode);
        }

        [Fact]
        public void SetsDefaultNackingMode_NORM_NACK_INFO_ONLY()
        {
            var nackingMode = NormNackingMode.NORM_NACK_INFO_ONLY;
            _normSession.SetDefaultNackingMode(nackingMode);
        }

        [Fact]
        public void SetsDefaultNackingMode_NORM_NACK_NORMAL()
        {
            var nackingMode = NormNackingMode.NORM_NACK_NORMAL;
            _normSession.SetDefaultNackingMode(nackingMode);
        }

        [SkippableFact(typeof(IOException))]
        public void SetsNackingMode()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedCommand = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                _normSession.SendCommand(expectedCommand, expectedCommand.Length, false);
                var normEventType = NormEventType.NORM_RX_CMD_NEW;
                var actualEvents = GetEvents();
                Assert.Contains(normEventType, actualEvents.Select(e => e.Type));
                var actualEvent = actualEvents.First(e => e.Type == normEventType);
                var actualNode = actualEvent.Node;
                Assert.NotNull(actualNode);
                actualNode.SetNackingMode(NormNackingMode.NORM_NACK_NONE);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [Fact]
        public void SetsDefaultRepairBoundary_NORM_BOUNDARY_BLOCK()
        {
            var repairBoundary = NormRepairBoundary.NORM_BOUNDARY_BLOCK;
            _normSession.SetDefaultRepairBoundary(repairBoundary);
        }

        [Fact]
        public void SetsDefaultRepairBoundary_NORM_BOUNDARY_OBJECT()
        {
            var repairBoundary = NormRepairBoundary.NORM_BOUNDARY_OBJECT;
            _normSession.SetDefaultRepairBoundary(repairBoundary);
        }

        [SkippableFact(typeof(IOException))]
        public void SetsRepairBoundary()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedCommand = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                _normSession.SendCommand(expectedCommand, expectedCommand.Length, false);
                var normEventType = NormEventType.NORM_RX_CMD_NEW;
                var actualEvents = GetEvents();
                Assert.Contains(normEventType, actualEvents.Select(e => e.Type));
                var actualEvent = actualEvents.First(e => e.Type == normEventType);
                var actualNode = actualEvent.Node;
                Assert.NotNull(actualNode);
                actualNode.SetRepairBoundary(NormRepairBoundary.NORM_BOUNDARY_OBJECT);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [Fact]
        public void SetsDefaultRxRobustFactor()
        {
            var rxRobustFactor = 2;
            _normSession.SetDefaultRxRobustFactor(rxRobustFactor);
        }

        [SkippableFact(typeof(IOException))]
        public void SetsRxRobustFactor()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedCommand = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                _normSession.SendCommand(expectedCommand, expectedCommand.Length, false);
                var normEventType = NormEventType.NORM_RX_CMD_NEW;
                var actualEvents = GetEvents();
                Assert.Contains(normEventType, actualEvents.Select(e => e.Type));
                var actualEvent = actualEvents.First(e => e.Type == normEventType);
                var actualNode = actualEvent.Node;
                Assert.NotNull(actualNode);
                actualNode.SetRxRobustFactor(2);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void GetsCommand()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedCommand = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                _normSession.SendCommand(expectedCommand, expectedCommand.Length, false);
                var normEventType = NormEventType.NORM_RX_CMD_NEW;
                var actualEvents = GetEvents();
                Assert.Contains(normEventType, actualEvents.Select(e => e.Type));
                var actualEvent = actualEvents.First(e => e.Type == normEventType);
                var actualNode = actualEvent.Node;
                Assert.NotNull(actualNode);

                var actualCommand = actualNode.Command;
                Assert.Equal(expectedCommand, actualCommand);
                var actualContent = Encoding.ASCII.GetString(actualCommand);
                Assert.Equal(expectedContent, actualContent);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void FreesBuffers()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedCommand = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                _normSession.SendCommand(expectedCommand, expectedCommand.Length, false);
                var normEventType = NormEventType.NORM_RX_CMD_NEW;
                var actualEvents = GetEvents();
                Assert.Contains(normEventType, actualEvents.Select(e => e.Type));
                var actualEvent = actualEvents.First(e => e.Type == normEventType);
                var actualNode = actualEvent.Node;
                Assert.NotNull(actualNode);
                actualNode.FreeBuffers();
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void RetainsAndReleases()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedCommand = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                _normSession.SendCommand(expectedCommand, expectedCommand.Length, false);
                var normEventType = NormEventType.NORM_RX_CMD_NEW;
                var actualEvents = GetEvents();
                Assert.Contains(normEventType, actualEvents.Select(e => e.Type));
                var actualEvent = actualEvents.First(e => e.Type == normEventType);
                var actualNode = actualEvent.Node;
                Assert.NotNull(actualNode);
                actualNode.Retain();
                actualNode.Release();
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void GetsObjectType_DATA()
        {
             StartSender();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                Assert.Equal(NormObjectType.NORM_OBJECT_DATA, normData.Type);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void GetsObjectType_FILE()
        {
             StartSender();

            var fileContent = GenerateTextContent();
            var fileName = Guid.NewGuid().ToString();
            var filePath = Path.Combine(_testPath, fileName);
            File.WriteAllText(filePath, fileContent);

            try
            {
                var normFile = _normSession.FileEnqueue(filePath);
                Assert.Equal(NormObjectType.NORM_OBJECT_FILE, normFile.Type);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void GetsObjectType_STREAM()
        {
             StartSender();

            var fileContent = GenerateTextContent();
            var data = Encoding.ASCII.GetBytes(fileContent);
            NormStream? normStream = null;

            try
            {
                var repairWindowSize = 1024 * 1024;
                normStream = _normSession.StreamOpen(repairWindowSize);
                Assert.Equal(NormObjectType.NORM_OBJECT_STREAM, normStream.Type);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                normStream?.Close(true);
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void GetsObjectSize()
        {
             StartSender();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedData = Encoding.ASCII.GetBytes(expectedContent);
            var expectedSize = Encoding.ASCII.GetByteCount(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                Assert.Equal(expectedSize, normData.Size);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void SetsObjectNackingMode_NORM_NACK_NONE()
        {
            StartSender();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                normData.SetNackingMode(NormNackingMode.NORM_NACK_NONE);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void SetsObjectNackingMode_NORM_NACK_INFO_ONLY()
        {
            StartSender();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                normData.SetNackingMode(NormNackingMode.NORM_NACK_INFO_ONLY);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void SetsObjectNackingMode_NORM_NACK_NORMAL()
        {
            StartSender();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                normData.SetNackingMode(NormNackingMode.NORM_NACK_NORMAL);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void GetsBytesPending()
        {
            StartSender();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedData = Encoding.ASCII.GetBytes(expectedContent);
            var expectedBytesPending = (long)0;

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                WaitForEvents();
                Assert.Equal(expectedBytesPending, normData.GetBytesPending());
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void CancelsObject()
        {
            StartSender();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                normData.Cancel();
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void RetainsObject()
        {
             StartSender();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                normData.Retain();
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void ReleasesObject()
        {
            StartSender();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                normData.Retain();
                normData.Release();
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void GetsSenderThrowsException()
        {
            StartSender();
            //Create data to write to the stream
            var expectedContent = GenerateTextContent();
            byte[] expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                Assert.Throws<IOException>(() => normData.Sender);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void GetsSender()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();

            //Set up cache directory
            var folderName = Guid.NewGuid().ToString();
            var cachePath = Path.Combine(_testPath, folderName);
            Directory.CreateDirectory(cachePath);
            _normInstance.SetCacheDirectory(cachePath);

            //Create data to be sent
            var expectedContent = GenerateTextContent();
            var expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0,     expectedData.Length);
                var normEventType = NormEventType.NORM_RX_OBJECT_COMPLETED;
                var actualEvents = GetEvents();
                Assert.Contains(normEventType, actualEvents.Select(e => e.Type));
                var actualEvent = actualEvents.First(e => e.Type == normEventType);
                var actualObject = actualEvent.Object;
                Assert.NotEqual(NormNode.NORM_NODE_INVALID, actualObject!.Sender);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void GetsObjectHashCode()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();

            //Set up cache directory
            var folderName = Guid.NewGuid().ToString();
            var cachePath = Path.Combine(_testPath, folderName);
            Directory.CreateDirectory(cachePath);
            _normInstance.SetCacheDirectory(cachePath);

            //Create data to be sent
            var expectedContent = GenerateTextContent();
            var expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                var normData = _normSession.DataEnqueue(expectedData, 0, expectedData.Length);
                var normEventType = NormEventType.NORM_RX_OBJECT_COMPLETED;
                var actualEvents = GetEvents();
                Assert.Contains(normEventType, actualEvents.Select(e => e.Type));
                var actualEvent = actualEvents.First(e => e.Type == normEventType);
                var actualObject = actualEvent.Object;
                var expectedHashCode = (int)actualObject!.Handle;
                var actualHashCode = actualObject.GetHashCode();
                Assert.Equal(expectedHashCode, actualHashCode);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void ObjectsEqual()
        {
             _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();

            //Set up cache directory
            var folderName = Guid.NewGuid().ToString();
            var cachePath = Path.Combine(_testPath, folderName);
            Directory.CreateDirectory(cachePath);
            _normInstance.SetCacheDirectory(cachePath);

            //Create data to be sent
            var expectedContent = GenerateTextContent();
            var expectedData = Encoding.ASCII.GetBytes(expectedContent);

            try
            {
                _normSession.DataEnqueue(expectedData, 0, expectedData.Length);

                var actualEvents = GetEvents();

                var firstNormEventType = NormEventType.NORM_RX_OBJECT_NEW;
                Assert.Contains(firstNormEventType, actualEvents.Select(e => e.Type));
                var firstEvent = actualEvents.First(e => e.Type == firstNormEventType);
                var firstObject = firstEvent.Object;
                Assert.NotNull(firstObject);

                var secondNormEventType = NormEventType.NORM_RX_OBJECT_COMPLETED;
                Assert.Contains(secondNormEventType, actualEvents.Select(e => e.Type));
                var secondEvent = actualEvents.First(e => e.Type == secondNormEventType);
                var secondObject = secondEvent.Object;
                Assert.NotNull(secondObject);

                Assert.True(firstObject.Equals(secondObject));
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void NormStreamHasVacancy()
        {
            StartSender();

            var fileContent = GenerateTextContent();
            var data = Encoding.ASCII.GetBytes(fileContent);
            NormStream? normStream = null;

            try
            {
                var repairWindowSize = 1024 * 1024;
                normStream = _normSession.StreamOpen(repairWindowSize);

                Assert.True(normStream.HasVacancy);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                normStream?.Close(true);
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void NormStreamGetsReadOffset()
        {
            StartSender();

            var fileContent = GenerateTextContent();
            var data = Encoding.ASCII.GetBytes(fileContent);
            NormStream? normStream = null;
            var expectedReadOffset = 0;

            try
            {
                var repairWindowSize = 1024 * 1024;
                normStream = _normSession.StreamOpen(repairWindowSize);

                Assert.Equal(expectedReadOffset, normStream.ReadOffset);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                normStream?.Close(true);
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void NormStreamSeeksMsgStart()
        {
            _normSession.SetLoopback(true);
            StartSender();
            StartReceiver();

            //Set up cache directory
            var folderName = Guid.NewGuid().ToString();
            var cachePath = Path.Combine(_testPath, folderName);
            Directory.CreateDirectory(cachePath);
            _normInstance.SetCacheDirectory(cachePath);

            var fileContent = GenerateTextContent();
            var data = Encoding.ASCII.GetBytes(fileContent);
            var dataOffset = 0;
            NormStream? normStream = null;

            try
            {
                var repairWindowSize = 1024 * 1024;
                normStream = _normSession.StreamOpen(repairWindowSize);

                var expectedBytesWritten = data.Length;
                normStream.Write(data, dataOffset, data.Length);

                normStream.MarkEom();
                normStream.Flush();
                var normEvents = GetEvents();
                AssertNormEvents(normEvents);

                var expectedNormEventType = NormEventType.NORM_RX_OBJECT_UPDATED;
                Assert.Contains(expectedNormEventType, normEvents.Select(e => e.Type));
                var normObjectEvent = normEvents.First(e => e.Type == expectedNormEventType);

                var receivedNormStream = Assert.IsType<NormStream>(normObjectEvent.Object);

                Assert.True(receivedNormStream.SeekMsgStart());
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                normStream?.Close(true);
                StopSender();
                StopReceiver();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void NormStreamSetsPushEnable()
        {
            StartSender();

            var fileContent = GenerateTextContent();
            var data = Encoding.ASCII.GetBytes(fileContent);
            NormStream? normStream = null;

            try
            {
                var repairWindowSize = 1024 * 1024;
                normStream = _normSession.StreamOpen(repairWindowSize);

                normStream.SetPushEnable(true);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                normStream?.Close(true);
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void NormStreamSetsAutoFlush_FLUSH_NONE()
        {
            StartSender();

            var fileContent = GenerateTextContent();
            var data = Encoding.ASCII.GetBytes(fileContent);
            NormStream? normStream = null;

            try
            {
                var repairWindowSize = 1024 * 1024;
                normStream = _normSession.StreamOpen(repairWindowSize);

                normStream.SetAutoFlush(NormFlushMode.NORM_FLUSH_NONE);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                normStream?.Close(true);
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void NormStreamSetsAutoFlush_FLUSH_PASSIVE()
        {
            StartSender();

            var fileContent = GenerateTextContent();
            var data = Encoding.ASCII.GetBytes(fileContent);
            NormStream? normStream = null;

            try
            {
                var repairWindowSize = 1024 * 1024;
                normStream = _normSession.StreamOpen(repairWindowSize);

                normStream.SetAutoFlush(NormFlushMode.NORM_FLUSH_PASSIVE);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                normStream?.Close(true);
                StopSender();
            }
        }

        [SkippableFact(typeof(IOException))]
        public void NormStreamSetsAutoFlush_FLUSH_ACTIVE()
        {
            StartSender();

            var fileContent = GenerateTextContent();
            var data = Encoding.ASCII.GetBytes(fileContent);
            NormStream? normStream = null;

            try
            {
                var repairWindowSize = 1024 * 1024;
                normStream = _normSession.StreamOpen(repairWindowSize);

                normStream.SetAutoFlush(NormFlushMode.NORM_FLUSH_ACTIVE);
            }
            catch (Exception)
            {
                throw;
            }
            finally
            {
                normStream?.Close(true);
                StopSender();
            }
        }
    }
}