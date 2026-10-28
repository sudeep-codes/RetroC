# References used in the RetroC paper (20, all ACM or Springer)

**How these were verified (28 Oct 2026).** Google Scholar blocked automated access with a CAPTCHA, so each entry was checked on the DOI registry itself: the Crossref API returns the publisher, authors, venue and pages that the publisher registered. The LNCS volume numbers (9034, 1973, 2894) were confirmed on SpringerLink. All 20 are indexed by Google Scholar under the same titles. To double-check any of them, search the title in quotes on scholar.google.com.

| # | Reference | Publisher | DOI |
|---|---|---|---|
| 1 | M. Zaharia et al., "Apache Spark: A unified engine for big data processing," Commun. ACM 59(11):56–65, 2016 | ACM | 10.1145/2934664 |
| 2 | J. Liu, E. Pacitti, P. Valduriez, M. Mattoso, "A survey of data-intensive scientific workflow management," J. Grid Comput. 13(4):457–493, 2015 | Springer | 10.1007/s10723-015-9329-8 |
| 3 | A. Mokhov, N. Mitchell, S. Peyton Jones, "Build systems à la carte," Proc. ACM Program. Lang. 2(ICFP):1–29, 2018 | ACM | 10.1145/3236774 |
| 4 | U. A. Acar, G. E. Blelloch, R. Harper, "Adaptive functional programming," POPL 2002, pp. 247–259 | ACM | 10.1145/503272.503296 |
| 5 | M. A. Hammer, K. Y. Phang, M. Hicks, J. S. Foster, "Adapton: Composable, demand-driven incremental computation," PLDI 2014, pp. 156–166 | ACM | 10.1145/2594291.2594324 |
| 6 | M. Abadi, F. McSherry, G. D. Plotkin, "Foundations of differential dataflow," FoSSaCS 2015, LNCS 9034, pp. 71–83 | Springer | 10.1007/978-3-662-46678-0_5 |
| 7 | D. G. Murray et al. (6 authors), "Naiad: A timely dataflow system," SOSP 2013, pp. 439–455 | ACM | 10.1145/2517349.2522738 |
| 8 | U. A. Acar, G. E. Blelloch, R. Harper, "Selective memoization," POPL 2003, pp. 14–25 | ACM | 10.1145/604131.604133 |
| 9 | J. A. Blakeley, P.-Å. Larson, F. W. Tompa, "Efficiently updating materialized views," SIGMOD 1986, pp. 61–71 | ACM | 10.1145/16894.16861 |
| 10 | A. Toshniwal et al., "Storm@twitter," SIGMOD 2014, pp. 147–156 | ACM | 10.1145/2588555.2595641 |
| 11 | Y. Cai, P. G. Giarrusso, T. Rendel, K. Ostermann, "A theory of changes for higher-order languages," PLDI 2014, pp. 145–155 | ACM | 10.1145/2594291.2594304 |
| 12 | E. D. Demaine, J. Iacono, S. Langerman, "Retroactive data structures," ACM Trans. Algorithms 3(2), Art. 13, 2007 | ACM | 10.1145/1240233.1240236 |
| 13 | P. Buneman, S. Khanna, W.-C. Tan, "Why and where: A characterization of data provenance," ICDT 2001, LNCS 1973, pp. 316–330 | Springer | 10.1007/3-540-44503-X_20 |
| 14 | J. Chen et al., "A survey of compiler testing," ACM Comput. Surv. 53(1):1–36, 2020 | ACM | 10.1145/3363562 |
| 15 | X. Yang, Y. Chen, E. Eide, J. Regehr, "Finding and understanding bugs in C compilers," PLDI 2011, pp. 283–294 | ACM | 10.1145/1993498.1993532 |
| 16 | T. Y. Chen et al., "Metamorphic testing: A review of challenges and opportunities," ACM Comput. Surv. 51(1):1–27, 2018 | ACM | 10.1145/3143561 |
| 17 | A. B. Kahn, "Topological sorting of large networks," Commun. ACM 5(11):558–562, 1962 | ACM | 10.1145/368996.369025 |
| 18 | G. L. Steele, D. Lea, C. H. Flood, "Fast splittable pseudorandom number generators," OOPSLA 2014, pp. 453–472 | ACM | 10.1145/2660193.2660195 |
| 19 | D. Clarke, S. Devadas, M. van Dijk, B. Gassend, G. E. Suh, "Incremental multiset hash functions and their application to memory integrity checking," ASIACRYPT 2003, LNCS 2894, pp. 188–207 | Springer | 10.1007/978-3-540-40061-5_12 |
| 20 | A. Georges, D. Buytaert, L. Eeckhout, "Statistically rigorous Java performance evaluation," OOPSLA 2007, pp. 57–76 | ACM | 10.1145/1297027.1297033 |

**Replacements made to meet the IEEE/ACM/Springer rule.** The best-known works on four topics were published elsewhere, so ACM or Springer works on the same topic were used:
- **Spark:** the NSDI 2012 paper (USENIX) was replaced by the *Communications of the ACM* article (#1).
- **Make / Airflow:** these (Wiley, documentation) were replaced by a Springer survey of workflow management systems (#2).
- **Differential dataflow:** the CIDR 2013 paper was replaced by "Foundations of Differential Dataflow" (#6).
- **Differential testing:** McKeeman 1998 (*Digital Technical Journal*) was replaced by the ACM compiler-testing survey (#14).

Publisher split: 16 ACM and 4 Springer. IEEE isn't represented, because the foundational papers on these topics appeared at ACM venues.
