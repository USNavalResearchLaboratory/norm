using Bogus;
using System.Text;

namespace Mil.Navy.Nrl.Norm.IntegrationTests
{
    /// <summary>
    /// Tests for NORM instance
    /// </summary>
    public class NormInstanceTests : IDisposable
    {
        /// <summary>
        /// The NORM instance
        /// </summary>
        private NormInstance _normInstance;
        private NormSession? _normSession;
        /// <summary>
        /// Determines if the NORM instance has been destroyed
        /// </summary>
        private bool _isDestroyed;

        private string _testPath;

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

        /// <summary>
        /// Default constructor for NORM instance tests
        /// </summary>
        /// <remarks>
        /// Creates the NORM instance.
        /// Initializes _isDestroyed to false.
        /// </remarks>
        public NormInstanceTests()
        {
            _normInstance = new NormInstance();
            _isDestroyed = false;
            var currentDirectory = Directory.GetCurrentDirectory();
            _testPath = Path.Combine(currentDirectory, Guid.NewGuid().ToString());
            CreateTestDirectory();
        }

        /// <summary>
        /// Destroy the NORM instance
        /// </summary>
        private void DestroyInstance()
        {
            if (!_isDestroyed) 
            {
                _normSession?.DestroySession();
                _normInstance.CloseDebugLog();
                _normInstance.DestroyInstance();
                _isDestroyed = true;
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
        /// Test for creating a NORM instance
        /// </summary>
        [Fact]
        public void CreatesNormInstance()
        {
            Assert.NotNull(_normInstance);
        }

        /// <summary>
        /// Test for creating a NORM instance
        /// </summary>
        [Fact]
        public void CreatesNormInstanceWithPriorityBoost()
        {
            _normInstance.DestroyInstance();
            _normInstance = new NormInstance(true);
            Assert.NotNull(_normInstance);
        }

        /// <summary>
        /// Test for destroying a NORM instance
        /// </summary>
        [Fact]
        public void DestroysNormInstance()
        {
            DestroyInstance();
        }

        /// <summary>
        /// Test for creating a NORM session
        /// </summary>
        [Fact]
        public void CreatesSession()
        {
            var faker = new Faker();
            var sessionAddress = "224.1.2.3";
            var sessionPort = faker.Internet.Port();
            var localNodeId = NormNode.NORM_NODE_ANY;

            _normSession = _normInstance.CreateSession(sessionAddress, sessionPort, localNodeId);
            Assert.NotNull(_normSession);
        }

        /// <summary>
        /// Test for throwing an exception when attempting to create a NORM session with an invalid session address
        /// </summary>
        [Fact]
        public void CreateSessionThrowsExceptionForInvalidSessionAddress()
        {
            var sessionAddress = "999.999.999.999";
            var sessionPort = 6003;
            var localNodeId = NormNode.NORM_NODE_ANY;

            Assert.Throws<IOException>(() => _normInstance.CreateSession(sessionAddress, sessionPort, localNodeId));
        }

        [Fact]
        public void StopInstance()
        {
            var sessionAddress = "224.1.2.3";
            var sessionPort = 6003;
            var localNodeId = NormNode.NORM_NODE_ANY;

            _normSession = _normInstance.CreateSession(sessionAddress, sessionPort, localNodeId);
        
            _normInstance.StopInstance();
        }

        [Fact]
        public void RestartInstance()
        {
            var sessionAddress = "224.1.2.3";
            var sessionPort = 6003;
            var localNodeId = NormNode.NORM_NODE_ANY;
            var expected = true;
            _normSession = _normInstance.CreateSession(sessionAddress, sessionPort, localNodeId);
        
            _normInstance.StopInstance();
            
            var actual = _normInstance.RestartInstance();
            Assert.Equal(expected,actual);
        }

         [Fact]
        public void SuspendInstance()
        {
            var sessionAddress = "224.1.2.3";
            var sessionPort = 6003;
            var localNodeId = NormNode.NORM_NODE_ANY;
            var expected = true;
            _normSession = _normInstance.CreateSession(sessionAddress, sessionPort, localNodeId);
        
            _normInstance.StopInstance();
            
            var actual = _normInstance.SuspendInstance();
            Assert.Equal(expected,actual);
        }
        [Fact]
        public void ResumeInstance()
        {
            var sessionAddress = "224.1.2.3";
            var sessionPort = 6003;
            var localNodeId = NormNode.NORM_NODE_ANY;
            _normSession = _normInstance.CreateSession(sessionAddress, sessionPort, localNodeId);
        
            _normInstance.StopInstance();

            _normInstance.ResumeInstance();
            Assert.NotNull(_normInstance);
        }
        [Fact]
        public void OpenDebugLog(){
            var fileName = Guid.NewGuid().ToString();
            var filePath = Path.Combine(_testPath, fileName);

            _normInstance.OpenDebugLog(filePath);
        }

        [Fact]
        public void CloseDebugLog()
        {
            _normInstance.CloseDebugLog();
        }

        [Fact]
        public void OpensDebugPipe()
        {
            var pipename = "test.pipe";
            Assert.Throws<IOException>(() => _normInstance.OpenDebugPipe(pipename));
        }

        [Fact]
        public void SetsDebugLevel()
        {
            var expectedDebugLevel = 12;
            _normInstance.DebugLevel = expectedDebugLevel;
            var actualDebugLevel = _normInstance.DebugLevel;
            Assert.Equal(expectedDebugLevel, actualDebugLevel);
        }

        [Fact]
        public void HasEventsFromTimeSpan()
        {
            var faker = new Faker();
            var sessionAddress = "224.1.2.3";
            var sessionPort = faker.Internet.Port();
            var localNodeId = NormNode.NORM_NODE_ANY;

            _normSession = _normInstance.CreateSession(sessionAddress, sessionPort, localNodeId);

            _normSession.StartSender(1024 * 1024, 1400, 64, 16);

            var dataContent = faker.Lorem.Paragraph();
            var data = Encoding.ASCII.GetBytes(dataContent);
            _normSession.DataEnqueue(data, 0, data.Length);

            Assert.True(_normInstance.HasNextEvent(TimeSpan.FromSeconds(1.5)));

            _normSession.StopSender();
        }

        [Fact]
        public void HasEventsFromSecondsAndMicroseconds()
        {
            var faker = new Faker();
            var sessionAddress = "224.1.2.3";
            var sessionPort = faker.Internet.Port();
            var localNodeId = NormNode.NORM_NODE_ANY;

            _normSession = _normInstance.CreateSession(sessionAddress, sessionPort, localNodeId);

            _normSession.StartSender(1024 * 1024, 1400, 64, 16);

            var dataContent = faker.Lorem.Paragraph();
            var data = Encoding.ASCII.GetBytes(dataContent);
            _normSession.DataEnqueue(data, 0, data.Length);

            Assert.True(_normInstance.HasNextEvent(1, 500000));

            _normSession.StopSender();
        }
    }
}
