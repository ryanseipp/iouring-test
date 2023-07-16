use std::io::{self, Read, Write};

use mio::{
    event::Event,
    net::{TcpListener, TcpStream},
    Events, Interest, Poll, Registry, Token,
};
use slab::Slab;

const SERVER: Token = Token(usize::MAX);

const RESPONSE: &[u8] =
    b"HTTP/1.1 200 OK\r\nContent-type: text/html\r\nContent-length: 17\r\n\r\nHave a nice day!\n";

fn handle_connection_event(
    registry: &Registry,
    connection: &mut TcpStream,
    event: &Event,
) -> io::Result<bool> {
    if event.is_writable() {
        match connection.write(RESPONSE) {
            Ok(n) if n < RESPONSE.len() => return Err(io::ErrorKind::WriteZero.into()),
            Ok(_) => registry.reregister(connection, event.token(), Interest::READABLE)?,
            Err(ref err) if err.kind() == io::ErrorKind::WouldBlock => {}
            Err(ref err) if err.kind() == io::ErrorKind::Interrupted => {
                return handle_connection_event(registry, connection, event)
            }
            Err(err) => return Err(err),
        }
    }

    if event.is_readable() {
        let mut connection_closed = false;
        let mut received_data = vec![0; 1024];
        let mut bytes_read = 0;

        loop {
            match connection.read(&mut received_data[bytes_read..]) {
                Ok(0) => {
                    connection_closed = true;
                    break;
                }
                Ok(n) => {
                    bytes_read += n;
                    if bytes_read == received_data.len() {
                        received_data.resize(received_data.len() + 1024, 0);
                    }
                }
                Err(ref err) if err.kind() == io::ErrorKind::WouldBlock => break,
                Err(ref err) if err.kind() == io::ErrorKind::Interrupted => continue,
                Err(err) => return Err(err),
            }
        }

        if bytes_read != 0 {
            registry.reregister(
                connection,
                event.token(),
                Interest::WRITABLE.add(Interest::READABLE),
            )?;
        }

        if connection_closed {
            return Ok(true);
        }
    }

    Ok(false)
}

fn main() -> io::Result<()> {
    let mut server = TcpListener::bind("127.0.0.1:8000".parse().unwrap())?;
    println!("Server listening on {}", server.local_addr()?);

    let mut poll = Poll::new()?;
    let mut events = Events::with_capacity(1024);

    poll.registry()
        .register(&mut server, SERVER, Interest::READABLE)?;

    let mut connections = Slab::with_capacity(2048);

    loop {
        poll.poll(&mut events, None)?;

        for event in events.iter() {
            match event.token() {
                SERVER => loop {
                    let (mut connection, _address) = match server.accept() {
                        Ok((connection, address)) => (connection, address),
                        Err(e) if e.kind() == io::ErrorKind::WouldBlock => {
                            break;
                        }
                        Err(e) => {
                            return Err(e);
                        }
                    };

                    let entry = connections.vacant_entry();
                    poll.registry().register(
                        &mut connection,
                        Token(entry.key()),
                        Interest::READABLE,
                    )?;
                    entry.insert(connection);
                },
                token => {
                    let done = if let Some(connection) = connections.get_mut(token.0) {
                        handle_connection_event(poll.registry(), connection, event)?
                    } else {
                        false
                    };

                    if done {
                        let mut connection = connections.remove(token.0);
                        poll.registry().deregister(&mut connection)?;
                    }
                }
            }
        }
    }
}
