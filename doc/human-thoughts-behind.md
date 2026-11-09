# Preface

Over the years, after far too few good coffes with far to smart people, after
far too many hours typing same things over and over agian, I've gathered some ideas. 
And while the ideas were sitting there rent free in my head, I got curious: what
if I've do what everyone else does these days - vibe it.

C is lovelly, but it lacks a bit of sugar, or maybe a few C++ features. Namelly:
* namespaces - any c project that grows, in the end grows into some naming
  convention. The names get long. Like `<module>_<struct>` then you get a function
  that's meant to work over this struct - so it's `<module>_<struct>_<function>` 
  or maybe it's `<module>_<function>_<struct>` because - why not?
  So it would be lovelly to have some namespacing capabilities in C to not write
  and read obvious things over and over again.
* RAII - C is lovelly, inheritance is over<__here put your negative opionion on it__> 
  but we miss one particular feature - automatic cleanup. Writting open/close 
  gets tedious, don't get me started on freeing memory. There is some support for
  RAII in GCC which is nice, having a bit of sugar would be even nicer.
* C macros are not very user friendly, hacks that do metaprogramming using these
  are fun to visit sometimes, but in the end - it's usually better to stay away.
  C++ templates are a beast, but not the easiest on the eyes kind of beast.
  Over the years compilers got better, introduction of auto, move semantics etc.
  made our life much better. buuuut - it would be nice to have simple to use
  semantics that is fully explicit. And it's not like there would be a need to
  have a lot of thingies in c to make some metaprogramming easier.
  It would pair beautifully with explicit contracts. It would be so nice to
  reinvent rust in c, without prior knowledge on rust would it?
* Homemade code generators are a ugly 90s dream that hunt's us sometime.
  Aaaanyways...

With 3$ of tokens, a bit of goodwill, a firm belief that at least namespaces could
be failry simple I went and spent it over a weekend. And this is how 0.1v of
'cx' or 'this thing' or as I call it in my head 'c+-' happened. Enjoy.

Please mind that:
* nearly everything was vibed. I had more fun I want to admit,so I'll probably
  continue on this jurney. The more I play, the more I want.
* The project is not meant to:
  * Replace C++ , or rust - that should be obvious...
  * Replace C - that should be obvious too...
* It's not reallity tested yet. I've did some sanity tests, and an example use,
  but nothing more really yet. Maybe after state machines a small embedded project
  would be nice.

It's a candy. A toy born from a question: how hard could it be?
