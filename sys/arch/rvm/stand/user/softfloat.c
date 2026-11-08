/* GCC's RVM libcalls return comparison/count results in word_mode (64 bits).
 * Imported compiler-rt uses 32-bit C int results. Adapt at the ABI boundary;
 * ordinary C int calls and compiler-rt's internal calls keep their native ABI.
 */
#define COMPARISONS(mode, type) \
    ADAPT(eq, mode, type) ADAPT(ne, mode, type) \
    ADAPT(le, mode, type) ADAPT(lt, mode, type) \
    ADAPT(ge, mode, type) ADAPT(gt, mode, type) ADAPT(unord, mode, type)
#define ADAPT(op, mode, type) \
    extern int __rvm_##op##mode##2(type, type); \
    long __##op##mode##2(type a, type b) { return __rvm_##op##mode##2(a, b); }
COMPARISONS(df, double)
COMPARISONS(sf, float)
extern int __rvm_clzdi2(long);
extern int __rvm_ctzdi2(long);
extern int __clzsi2(unsigned);
extern int __ctzsi2(unsigned);
long __clzdi2(long a) { return __rvm_clzdi2(a); }
long __ctzdi2(long a) { return __rvm_ctzdi2(a); }
/* The GCC compatibility aliases share the comparison word ABI. */
long __cmpdf2(double a, double b) { return __rvm_ledf2(a, b); }
long __cmpsf2(float a, float b) { return __rvm_lesf2(a, b); }
