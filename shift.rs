use std::fs::File; // QUESTION/DISCUSSION: why is File capitalised but others aren't? Because it's a trait, I guess?
use std::os::unix::io::AsRawFd;
use std::io::Read;
use std::io::Seek; // QUESTION/DISCUSSION: rustc is good at suggesting traits to import
use std::io::SeekFrom;
// use std::process; // was used for exit, but I'm not exiting...

extern crate time;


// QUESTION/DISCUSSION: macros are distinct syntax-wise from
// functions: they always have ! on the end; woo woo pattern matching!
// though this is quite a different language to learn. Basic ideas
// familiar from Haskell though. Some of it has the cursedness of
// regexp "concise" syntax.
macro_rules! dbg {

 ($($x:expr),*)  => {{ 
    print!("debug: ");
    println!($($x),*);
 }};

}



// QUESTION/DISCUSSION:
// it was a bit of a mission to find poll() in libc
// as a lot of discussion was more about how to do common
// things without using poll, but not what I want to do
// with a GPIO pin. Frothy mouthed pragmatism of a lanugage
// with a point - don't I know that from haskell...
extern crate libc;

// QUESTION/DISCUSSION: Haskell style ":" meaning "has type"

// The number of readings per second that will go into the
// shift buffer.
const RPS: usize = 500;
const BUFSIZE: usize = RPS * 60;

const BUF_INVALID : u8 = 2;
const TENTHS_INVALID_SENTINEL : i64 = -1;

fn main() {
  print_banner();

  println!("TODO: get NTP shm");

  dbg!("opening GPIO {}", "foo");

  let gpio_filename = "/sys/class/gpio/gpio25/value";
  // let gpio_filename = "./test";
  let mut f = File::open(gpio_filename).expect("opening GPIO port");

  // TODO: this will all be mutable one way or another
  // but leave them non-mutable for now to see where rust decides
  // I need to mutate them.

  // QUESTION/DISCUSSION:
  // Vec is a growable vector: we don't need that expandability...
  // so what is a better format? A regular array?
  // Also, what is this "mut" describing? the "buffer" the
  //   reference can change? or the heap content? or both?
  let mut buffer : Vec<u8> = vec![BUF_INVALID; BUFSIZE];
  let mut bufoff = 0;
  let mut oldc : u8 = BUF_INVALID;
  let mut oldtenths : i64 = TENTHS_INVALID_SENTINEL;
  

  dbg!("done initialising");

  loop {

    // QUESTION/DISCUSSION: turn this into a macro that is
    // a long description when in debug mode and a single character
    // when in non-debug mode.
    print!("."); // indicate polling

    let fd = f.as_raw_fd();

    let mut pfd = libc::pollfd {
        fd: fd,
        events: libc::POLLPRI,
        revents: 0 
      };

    // QUESTION/DISCUSSION: ok so semicolon on the end is significant
    // here: we return the last statement value if it doesn't have
    // a semicolon... if there is a semicolon then there's an implicit
    // void statement? 

    let pollret : i32 = unsafe { libc::poll(&mut pfd, 1, 5000) };
    let edge_time = time::get_time();
    // ^ Get this time as close to the read as possible, because
    //   that should be as close to the actual edge as possible.
    println!("edge_time: {} . {}", edge_time.sec, edge_time.nsec);

    // even though we time-out here, it is safe to continue with the
    // rest of the processing: we'll treat it as two (or more)
    //  '0's in a row, or two '1's in a row.
    if pollret == 0 {
      print!("T");
    }

  println!("TODO: MAINLOOP: figure out pulse len and fill in");

  // QUESTION/DISCUSSION: there are too many casts here for my taste.
  // - the C version has some long long casts at the equivalent place
  // but the rust version has more.
    let newtenths = (edge_time.sec as i64) * (RPS as i64) + (edge_time.nsec as i64) / (1000000000 / (RPS as i64));


  // if we've advanced more than one slot since the last edge
    if newtenths != oldtenths && oldtenths != TENTHS_INVALID_SENTINEL {
      let numslots : i64 = newtenths - oldtenths - 1;
      dbg!("debug: Will fill in {} slots", numslots);
      assert!( numslots <= (BUFSIZE as i64) ); // if our poll timeout is more than a minute, we might end up with more than one buffer worth to fill - so need to be sure that the poll timeout is less than a minute. it is 5s at the moment, which is plenty.
// QUESTION/DISCUSSION: this gives a compiler warning because
// 'i' is unused. What is the correct way to do a loop n times?
      for i in 0 .. numslots {
        buffer[bufoff] = oldc;
        bufoff = (bufoff+1) % BUFSIZE;
      }

  println!("TODO: MAINLOOP: checkdecode");
    }
    oldtenths = newtenths;

    dbg!("seek in GPIO file to zero");

    // QUESTION/DISCUSSION: rustc does a good job warning about
    // unused return value here if expect() is not used to capture
    // it.
    f.seek(SeekFrom::Start(0)).expect("seeking to start of GPIO file");

    dbg!("reading a byte from GPIO");
    let mut gpio_value = [0; 1]; // TODO: what does [0;1] mean?
    f.read_exact(&mut gpio_value).expect("reading a byte from GPIO");
    dbg!("read from gpio: value {}", gpio_value[0]);

    oldc=gpio_value[0];

  println!("TODO: MAINLOOP: handle inhibit decode"); 
  }
}

fn print_banner() {
  println!("MSF decoder - Rust version");
  println!("Copyright 2017 Ben Clifford benc@hawaga.org.uk");
  println!("Key:");
  println!("  . = waiting for edge on GPIO input");
  println!("  T = timeout without edge on GPIO input");
  println!("  > = starting decode");
  println!("  X0 = decode failed: insufficient zeroes in scan zone");
  println!("  X1 = decode failed: insufficient ones in scan zone");
  println!("  * = decode inhibition ended");
}
