use crate::error::Result;
use crate::session::Session;
use std::net::IpAddr;
use std::fmt;
use std::str::FromStr;

/// Ergonomic multicast configuration for NORM sessions.
///
/// This struct provides a builder-style API for configuring multicast options
/// for a NORM session. It handles the details of setting up the various multicast
/// options and applying them to a session.
#[derive(Debug, Clone)]
pub struct MulticastConfig {
    /// The multicast address
    address: String,
    /// The port
    port: u16,
    /// The network interface to use for multicast
    interface: Option<String>,
    /// The time-to-live (TTL) for multicast packets
    ttl: Option<u8>,
    /// Whether to enable loopback
    loopback: Option<bool>,
    /// The source address for SSM (Source-Specific Multicast)
    ssm_source: Option<String>,
    /// The type of service (TOS) value for IP packets
    tos: Option<u8>,
}

impl MulticastConfig {
    /// Create a new multicast configuration
    ///
    /// # Arguments
    /// * `address` - The multicast address
    /// * `port` - The port number
    ///
    /// # Returns
    /// A new multicast configuration
    pub fn new(address: impl Into<String>, port: u16) -> Self {
        Self {
            address: address.into(),
            port,
            interface: None,
            ttl: None,
            loopback: None,
            ssm_source: None,
            tos: None,
        }
    }

    /// Set the network interface for multicast
    ///
    /// # Arguments
    /// * `interface` - The name of the interface to use
    ///
    /// # Returns
    /// Self for method chaining
    pub fn interface(mut self, interface: impl Into<String>) -> Self {
        self.interface = Some(interface.into());
        self
    }

    /// Set the time-to-live (TTL) for multicast packets
    ///
    /// # Arguments
    /// * `ttl` - The TTL value (0-255)
    ///
    /// # Returns
    /// Self for method chaining
    pub fn ttl(mut self, ttl: u8) -> Self {
        self.ttl = Some(ttl);
        self
    }

    /// Enable or disable multicast loopback
    ///
    /// # Arguments
    /// * `enable` - Whether to enable loopback
    ///
    /// # Returns
    /// Self for method chaining
    pub fn loopback(mut self, enable: bool) -> Self {
        self.loopback = Some(enable);
        self
    }

    /// Set the source address for SSM (Source-Specific Multicast)
    ///
    /// # Arguments
    /// * `source` - The source address
    ///
    /// # Returns
    /// Self for method chaining
    pub fn ssm_source(mut self, source: impl Into<String>) -> Self {
        self.ssm_source = Some(source.into());
        self
    }

    /// Set the type of service (TOS) value for IP packets
    ///
    /// # Arguments
    /// * `tos` - The TOS value (0-255)
    ///
    /// # Returns
    /// Self for method chaining
    pub fn tos(mut self, tos: u8) -> Self {
        self.tos = Some(tos);
        self
    }

    /// Apply the configuration to a session
    ///
    /// # Arguments
    /// * `session` - The session to apply the configuration to
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if any configuration option could not be applied
    pub fn apply(&self, session: &Session) -> Result<()> {
        if let Some(ref interface) = self.interface {
            session.set_multicast_interface(interface)?;
        }

        if let Some(ttl) = self.ttl {
            session.set_ttl(ttl)?;
        }

        if let Some(loopback) = self.loopback {
            session.set_multicast_loopback(loopback)?;
        }

        if let Some(ref source) = self.ssm_source {
            session.set_ssm(source)?;
        }

        if let Some(tos) = self.tos {
            session.set_tos(tos)?;
        }

        Ok(())
    }

    /// Get the multicast address
    ///
    /// # Returns
    /// The multicast address as a string
    pub fn address(&self) -> &str {
        &self.address
    }

    /// Get the port number
    ///
    /// # Returns
    /// The port number
    pub fn port(&self) -> u16 {
        self.port
    }
}

impl fmt::Display for MulticastConfig {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}:{}", self.address, self.port)?;

        if let Some(ref interface) = self.interface {
            write!(f, " on {}", interface)?;
        }

        if let Some(ttl) = self.ttl {
            write!(f, " ttl={}", ttl)?;
        }

        if let Some(loopback) = self.loopback {
            write!(f, " loopback={}", loopback)?;
        }

        if let Some(ref source) = self.ssm_source {
            write!(f, " ssm_source={}", source)?;
        }

        if let Some(tos) = self.tos {
            write!(f, " tos={}", tos)?;
        }

        Ok(())
    }
}

/// Convenient macro for multicast configuration
///
/// # Examples
///
/// ```
/// # use norm::multicast;
/// let config = multicast!("224.1.2.3", 6003, {
///     ttl: 64,
///     interface: "eth0",
///     loopback: true,
/// });
/// ```
#[macro_export]
macro_rules! multicast {
    ($addr:expr, $port:expr) => {
        $crate::MulticastConfig::new($addr, $port)
    };
    ($addr:expr, $port:expr, { $($key:ident: $value:expr),* $(,)? }) => {{
        let mut config = $crate::MulticastConfig::new($addr, $port);
        $(
            config = config.$key($value);
        )*
        config
    }};
}

/// Extension trait for NORM sessions to apply multicast configurations
pub trait MulticastExt {
    /// Apply a multicast configuration to the session
    ///
    /// # Arguments
    /// * `config` - The multicast configuration to apply
    ///
    /// # Returns
    /// `Ok(())` on success or an `Err` if the configuration could not be applied
    fn with_multicast(&self, config: &MulticastConfig) -> Result<&Self>;
}

impl MulticastExt for Session {
    fn with_multicast(&self, config: &MulticastConfig) -> Result<&Self> {
        config.apply(self)?;
        Ok(self)
    }
}

/// Check if an IP address is a multicast address
///
/// # Arguments
/// * `addr` - The IP address to check
///
/// # Returns
/// `true` if the address is a multicast address, `false` otherwise
pub fn is_multicast_address(addr: &str) -> bool {
    if let Ok(ip) = IpAddr::from_str(addr) {
        match ip {
            IpAddr::V4(ipv4) => ipv4.is_multicast(),
            IpAddr::V6(ipv6) => ipv6.is_multicast(),
        }
    } else {
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_multicast_config_builder() {
        let config = MulticastConfig::new("224.1.2.3", 6003)
            .ttl(64)
            .interface("eth0")
            .loopback(true)
            .ssm_source("192.168.1.1")
            .tos(0x10);

        assert_eq!(config.address(), "224.1.2.3");
        assert_eq!(config.port(), 6003);
        assert_eq!(config.ttl, Some(64));
        assert_eq!(config.interface.as_deref(), Some("eth0"));
        assert_eq!(config.loopback, Some(true));
        assert_eq!(config.ssm_source.as_deref(), Some("192.168.1.1"));
        assert_eq!(config.tos, Some(0x10));
    }

    #[test]
    fn test_multicast_config_display() {
        let config = MulticastConfig::new("224.1.2.3", 6003)
            .ttl(64)
            .loopback(true);

        let display = format!("{}", config);
        assert!(display.contains("224.1.2.3:6003"));
        assert!(display.contains("ttl=64"));
        assert!(display.contains("loopback=true"));
    }

    #[test]
    fn test_is_multicast_address_ipv4() {
        // Valid IPv4 multicast addresses (224.0.0.0 to 239.255.255.255)
        assert!(is_multicast_address("224.0.0.0"));
        assert!(is_multicast_address("224.1.2.3"));
        assert!(is_multicast_address("239.255.255.255"));

        // Invalid IPv4 multicast addresses
        assert!(!is_multicast_address("192.168.1.1"));
        assert!(!is_multicast_address("10.0.0.1"));
        assert!(!is_multicast_address("127.0.0.1"));
    }

    #[test]
    fn test_is_multicast_address_ipv6() {
        // Valid IPv6 multicast addresses (start with ff00::/8)
        assert!(is_multicast_address("ff02::1"));
        assert!(is_multicast_address("ff05::1:3"));

        // Invalid IPv6 multicast addresses
        assert!(!is_multicast_address("fe80::1"));
        assert!(!is_multicast_address("::1"));
        assert!(!is_multicast_address("2001:db8::1"));
    }

    #[test]
    fn test_is_multicast_address_invalid() {
        // Invalid IP addresses
        assert!(!is_multicast_address("not.an.ip"));
        assert!(!is_multicast_address(""));
        assert!(!is_multicast_address("999.999.999.999"));
    }

    #[test]
    fn test_multicast_macro() {
        let config = multicast!("224.1.2.3", 6003, {
            ttl: 64,
            loopback: true,
        });

        assert_eq!(config.address(), "224.1.2.3");
        assert_eq!(config.port(), 6003);
        assert_eq!(config.ttl, Some(64));
        assert_eq!(config.loopback, Some(true));
    }

    #[test]
    fn test_multicast_macro_simple() {
        let config = multicast!("224.1.2.3", 6003);

        assert_eq!(config.address(), "224.1.2.3");
        assert_eq!(config.port(), 6003);
        assert_eq!(config.ttl, None);
        assert_eq!(config.interface, None);
    }
}